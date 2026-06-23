# CLAUDE.md

Документация проекта для будущих сессий. Описывает реальное состояние кода (а не устаревший README).

## ⚠️ Перед началом работы (обязательно)

**1. Сначала — граф знаний, потом код.** Прежде чем отвечать на вопросы об архитектуре или искать по файлам:
- Открой `graphify-out/wiki/index.md` — это точка входа в граф знаний проекта (сущности, связи, сообщества).
- Для навигации/обзоров используй `graphify-out/obsidian/_COMMUNITY_*.md`, для сырых связей — `graphify-out/graph.json`.
- Сырые исходники читай только когда граф не дал ответа или нужны точные строки.
- Запросы к графу: `/graphify query "вопрос"`, путь между узлами: `/graphify path "A" "B"`, объяснение узла: `/graphify explain "X"`.

**2. После изменений кода — обнови граф:** `/graphify . --update --obsidian` (инкрементально). Сборочные артефакты (`data/static/`, минифицированный `main.*.js`) в граф не включаем — это шум. `graphify-out/` в `.gitignore`, строится локально.

**3. Самообучение на ошибках.** В начале сессии прочитай `tasks/lessons.md`. После КАЖДОЙ правки/замечания от пользователя или найденного бага — допиши туда урок: что пошло не так, почему, и правило, которое не даст повторить ошибку. Не повторяй ошибки, уже записанные там.

## Что это

**«Умные Ворота»** — прошивка для **ESP32 DevKit (38 pin)** с радиомодулем **CC1101**. Принимает RF-брелоки 300–928 МГц, распознаёт протокол (CAME, Nice, Keeloq, Princeton, Holtek, BFT, Somfy, StarLine, Security+ и др.), запоминает «доверенные» ключи и при их получении даёт импульс на реле/ворота. Управление и обучение — через веб-интерфейс по Wi-Fi. Декодеры портированы из прошивки **Flipper Zero Unleashed**.

> ⚠️ **README.md устарел**: там RF433-приёмник, дисплей ST7789 и джойстик — в коде их **нет**. Источник правды — этот файл, граф знаний и `src/`.

## Железо и пины (см. `src/main.cpp`)

| Назначение | GPIO |
|---|---|
| CC1101 CS (CSN) | 5 |
| CC1101 GDO0 (RAW data) | 4 |
| CC1101 GDO2 (clock) | 2 |
| SPI SCK / MISO / MOSI | 18 / 19 / 23 |
| Реле/LED (импульс ворот) | 12 |
| GSM SIM800L RX / TX (UART2) | 16 / 17 |

GSM (SIM800L) физически не задействован — вызовы `GSMManager` закомментированы в `main.cpp`.

## Сборка, заливка, логи

PlatformIO, env `esp32dev`, Arduino framework. Зависимости (`platformio.ini`): RadioLib 6.6, ArduinoJson 7, TinyGSM, WebSockets. Кастомная таблица разделов — `partitions.csv`.

```bash
platformio run                      # сборка прошивки
platformio run --target upload      # залить прошивку
platformio run --target uploadfs    # залить SPIFFS (фронтенд из data/)
platformio device monitor --baud 115200
./upload.sh                         # всё сразу: build React → copy в data/ → upload → uploadfs
```

### Хранение данных — раздел `userdata` (важно)
Таблица разделов (`partitions.csv`) выделяет отдельный NVS-раздел **`userdata` (256KB @ 0x250000)** под ключи, телефоны и настройки. `main.cpp` пишет туда напрямую через `nvs_*_from_partition("userdata", ...)` (функции `saveSystemState`/`loadSystemState`). Раздел **не затрагивается** ни при `upload`, ни при `uploadfs` — данные пользователя переживают любую перепрошивку. Системный `nvs` (0x9000) — отдельно. Полное стирание всего: `platformio run --target erase`.

## Архитектура

```
src/
  main.cpp              # веб-сервер (:80) + WebSocket (:81) + бизнес-логика + userdata NVS
  CC1101Manager.cpp/.h  # ядро RF: RAW OOK приём, захват импульсов, прогон через мультидекодер
  SubGhzProtocols.cpp/.h# legacy config-массив ALL_PROTOCOLS[] (17 шт), большинство — алиасы
  GateControl.cpp/.h    # импульс на реле (GPIO12)
  GSMManager.cpp/.h     # GSM (заглушка, не подключён)
  WiFiManager.cpp/.h    # Wi-Fi
  infrastructure/Logger # логи в Serial + WebSocket
include/
  SubGhzDecoderBase.h   # базовый класс декодера (state machine) + SubGhzMultiDecoderT
  SubGhzDecoder.h       # SubGhzMultiDecoder: 40 декодеров + ProtoGenericOOK fallback
  protocols/Proto*.h    # сами декодеры (CAME, NiceFlo, Keeloq, Princeton, NeroRadio,
                        #   GateTx, StarLine, Batch2, Batch3) — порт Flipper Unleashed
data/                   # СОБРАННЫЙ React-фронт (грузится в SPIFFS); не править руками; в .gitignore
smart-gate-frontend/    # исходники React/TS (страницы: Keys, Phones, WiFi, Settings)
partitions.csv          # таблица разделов с userdata
reference_*.c           # референс из Flipper (справка, не компилируется)
```

`main.cpp` — монолит (~1670 строк): HTTP-эндпоинты, режим обучения, сравнение/верификация ключей, сериализация состояния в `userdata`.

## Декодирование (главное изменение)

Используется **новый мультидекодер в стиле Flipper Zero**, а НЕ старый перебор конфигов:

1. **`CC1101Manager::onInterrupt()`** (IRAM, фронт на GDO0) — пишет длительности импульсов в `rawSignalTimings[]`/`rawSignalLevels[]`; склейка шума < 40 мкс; конец пакета по большому gap.
2. **`checkReceived()`** (в `loop()`): валидация сигнала, фильтр RSSI (< −100 dBm — шум), затем **прогон буфера импульс-за-импульсом через `SubGhzMultiDecoder::feed()`** (CC1101Manager.cpp ~строка 877). Каждый из ~40 декодеров — свой state machine со своей преамбулой; **первый полностью распознавший пакет побеждает** (`break`). Fallback — `ProtoGenericOOK` (любой OOK 2:1…6:1, ≥20 бит). Если ничего — `RAW/Unknown` (код = хеш).
3. Дальше — фильтры качества кода (нули/единицы/повторяющиеся паттерны) и дедупликация по хешу/коду.
4. **`main.cpp::loop()`**: в `learningMode` сигналы `RAW/Unknown`/`RAW/Custom` **отбрасываются как шум** (сохраняются только декодированные протоколы); иначе `isKeyMatch()` ищет совпадение и `verifyKeySignal()` требует N повторов → `GateControl::triggerGatePulse()`.

**Список декодеров** — в конструкторе `SubGhzMultiDecoder` (`include/SubGhzDecoder.h`): CAME/Twee/Atomo, Nice FLO/FloR-S, Nero Radio/Sketch, Keeloq, GateTX, Holtek, Linear, Chamberlain, Hormann, FAAC SLH, StarLine, Somfy Telis/Keytis, BFT Mitto, Dooya, Marantec, Clemsa, Doitrand, Phoenix V2, Magellan, Legrand, KingGates, Ansonic, SMC5326, Honeywell(+WDB), Alutech AT-4N, Holtek HT12X, Linear Delta3, Security+ v2, Megacode, iDo, Mastercode, PowerSmart + GenericOOK.

> Старые `SubGhzProtocols.cpp` (`ALL_PROTOCOLS[]`, дедуп до 17 конфигов) и `tryDecodeKnownProtocols()`/`decodeWithProtocols()` в CC1101Manager — **legacy**, в основном пути приёма не используются. Не путать с активным мультидекодером.

## Сравнение ключей (main.cpp)

- `isKeyMatch()`: строгое совпадение протокола + частоты (±1 МГц); ≤32 бит — точное совпадение `bitString`, >32 бит — 95% похожести; fallback по `code` + TE.
- `verifyKeySignal()`: число требуемых повторов зависит от качества декода/RSSI/длины протокола (1–5). В обучении принимает сразу.
- Дедуп для UI: `isDuplicateForDisplay()` + `detectionHistory`.

Структуры: `ReceivedKey` (CC1101Manager.h), `SubGhzDecoderResult` (SubGhzDecoderBase.h), `KeyEntry`/`SystemState` (main.cpp).

## Конфигурация радио (`CC1101Manager::init()`)

ASK/OOK, **битрейт 20.0 kbps**, **RX BW 135 кГц**, частота по умолчанию 433.92 МГц (меняется рантайм через `/api/frequency/set`, 300–928 МГц). Direct/RAW режим: GDO0 = данные, GDO2 = clock. (Прежние 3.79 kbps / 58 кГц — устаревшие, заменены под Flipper OOK650.)

## Веб-API (HTTP :80, WebSocket :81)

`GET/POST /api/keys`, `/api/keys/learn|stop|status|delete|update`, `/api/phones[...]`, `/api/wifi/scan|connect`, `/api/frequency[/set]`, `/api/cc1101/config`, `/api/gate/trigger`. WebSocket-события: `log`, `key_received`, `key_added`, `wifi_status`. Статика — из SPIFFS. Wi-Fi: всегда AP `SmartGate-Config` / `12345678` (`192.168.4.1`) + mDNS `smartgate.local`; при сохранённых креды — коннект к роутеру.

## Известные ограничения / TODO

- **Rolling code частично:** есть декодеры Keeloq / Security+ v2, но сравнение ключей статическое — настоящий динамический код по-прежнему не валидируется криптографически.
- **FSK/MSK/GFSK** — реально работает только OOK/ASK; часть 868-МГц брелоков не возьмётся.
- **Только приём (RX).** TX (эмуляция/отправка ключа) не реализован.
- README рассинхронизирован с кодом; `main.cpp` стоило бы разнести по модулям; в CC1101Manager остался legacy-декодер.
- `data/` — артефакт сборки фронта; UI правится в `smart-gate-frontend/` и пересобирается (`./upload.sh`).

## Стиль

Комментарии и логи — на русском, как в коде. Без временных костылей, искать корневую причину. Перед «готово» — проверять реальным запуском/логами, а не только сборкой.
