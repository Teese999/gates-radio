# CLAUDE.md

Документация проекта для будущих сессий. Описывает реальное состояние кода (а не устаревший README).

## ⚠️ Перед началом работы (обязательно)

**1. Сначала — граф знаний, потом код.** Прежде чем отвечать на вопросы об архитектуре или искать по файлам:
- Открой `graphify-out/wiki/index.md` — это точка входа в граф знаний проекта (сущности, связи, сообщества).
- Для навигации/обзоров используй `graphify-out/obsidian/_COMMUNITY_*.md`, для сырых связей — `graphify-out/graph.json`.
- Сырые исходники читай только когда граф не дал ответа или нужны точные строки.
- Запросы к графу: `/graphify query "вопрос"`, путь между узлами: `/graphify path "A" "B"`, объяснение узла: `/graphify explain "X"`.

**2. После изменений кода — обнови граф:** `/graphify . --update --obsidian` (инкрементально переэкстрактит только изменённые файлы). Сборочные артефакты (`data/static/`, минифицированный `main.*.js`) в граф не включаем — это шум.

**3. Самообучение на ошибках.** В начале сессии прочитай `tasks/lessons.md`. После КАЖДОЙ правки/замечания от пользователя или найденного бага — допиши туда урок: что пошло не так, почему, и правило, которое не даст повторить ошибку. Это живой файл, перечитывай и применяй его. Не повторяй ошибки, уже записанные там.

## Что это

**«Умные Ворота»** — прошивка для **ESP32 DevKit (38 pin)** с радиомодулем **CC1101**. Принимает RF-брелоки 300–928 МГц (CAME, Nice, Keeloq, Princeton, PT2262, Holtek, FAAC, BFT, Somfy и др.), запоминает «доверенные» ключи и при их получении даёт импульс на реле/ворота. Управление и обучение — через веб-интерфейс по Wi-Fi. Логика декодирования сделана в стиле Flipper Zero (RAW OOK + перебор протоколов).

> ⚠️ **README.md устарел**: там описаны RF433-приёмник, дисплей ST7789 и джойстик. Их в коде **нет**. Источник правды — этот файл и `src/`.

## Железо и пины (см. `src/main.cpp`)

| Назначение | GPIO |
|---|---|
| CC1101 CS (CSN) | 5 |
| CC1101 GDO0 (RAW data) | 4 |
| CC1101 GDO2 (clock) | 2 |
| SPI SCK / MISO / MOSI | 18 / 19 / 23 |
| Реле/LED (импульс ворот) | 12 |
| GSM SIM800L RX / TX (UART2) | 16 / 17 |

GSM (SIM800L) физически не задействован — `GSMManager::init/handleGSM` закомментированы в `main.cpp`.

## Сборка, заливка, логи

PlatformIO, env `esp32dev`, Arduino framework. Зависимости (`platformio.ini`): RadioLib 6.6, ArduinoJson 7, TinyGSM, WebSockets.

```bash
platformio run                      # сборка прошивки
platformio run --target upload      # залить прошивку (БЕЗ erase — сохраняет NVS)
platformio run --target uploadfs    # залить SPIFFS (фронтенд из data/)
platformio device monitor --baud 115200
./upload.sh                         # всё сразу: build React → copy в data/ → upload → uploadfs
```

**Важно про NVS:** ключи, телефоны и Wi-Fi хранятся в NVS через `Preferences`. Прошивка заливается без стирания flash, чтобы данные пережили перепрошивку. Полное стирание — только явно: `platformio run --target erase`.

## Архитектура

```
src/
  main.cpp              # веб-сервер (:80) + WebSocket (:81) + вся бизнес-логика
  CC1101Manager.cpp/.h  # ядро RF: RAW OOK приём, захват импульсов, декод протоколов
  SubGhzProtocols.cpp/.h# ~50 конфигов протоколов + массив ALL_PROTOCOLS[]
  GateControl.cpp/.h    # импульс на реле (GPIO12)
  GSMManager.cpp/.h     # GSM (заглушка, не подключён)
  WiFiManager.cpp/.h    # Wi-Fi
  infrastructure/
    Logger.cpp/.h       # логи в Serial + WebSocket
    StorageService.h    # обёртка хранилища
include/                # заголовки
data/                   # СОБРАННЫЙ React-фронтенд (грузится в SPIFFS); не править руками
smart-gate-frontend/    # исходники React/TS (страницы: Keys, Phones, WiFi, Settings)
reference_*.c           # референс-код из Flipper Zero (только для справки, не компилируется)
```

`main.cpp` — монолит (~1430 строк): все HTTP-эндпоинты, режим обучения, сравнение/верификация ключей, сериализация состояния в NVS живут в одном файле.

## Поток приёма ключа

1. **`CC1101Manager::onInterrupt()`** (IRAM, по фронту на GDO0) — пишет длительности импульсов в `rawSignalTimings[]`/`rawSignalLevels[]`. Склеивает шумовые импульсы < 40 мкс, детектит конец сигнала по большому gap (`END_GAP_US`), выставляет `receivedFlag`.
2. **`checkReceived()`** (вызывается в `loop()`): `signalLooksValid()` → `analyzePulsePattern()` (авто-TE) → фильтр RSSI/шума → дедуп по хешу/коду.
3. **`tryDecodeKnownProtocols()`** → для каждого протокола из `ALL_PROTOCOLS[]` перебирает варианты TE (×7), инверсию, manchester и смещение преамбулы (skip); `decodeProtocolRCSwitch()` собирает биты; выбирается лучший результат (приоритет полному декоду). Не распознал → `RAW/Unknown` (код = хеш сигнала).
4. **`main.cpp::loop()`**: если `learningMode` — добавляет ключ (с фильтром близости `LEARNING_MODE_MIN_RSSI = -70 dBm`); иначе `isKeyMatch()` ищет совпадение среди сохранённых и `verifyKeySignal()` требует N повторов (адаптивно) → `GateControl::triggerGatePulse()`.

Ключевые структуры: `ReceivedKey` (CC1101Manager.h), `KeyEntry` / `KeyRecognition` / `SystemState` (main.cpp), `SubGhzProtocolConfig` (SubGhzProtocols.h).

## Сравнение ключей (main.cpp)

- `isKeyMatch()`: строгое совпадение протокола + частоты (±1 МГц); ≤32 бит — точное совпадение `bitString`, >32 бит — 95% похожести (`compareBitStrings`); fallback по `code` + TE (±30%).
- `verifyKeySignal()`: число требуемых повторов зависит от качества декода/RSSI/длины протокола (1–5). В режиме обучения принимает сразу.
- Дедуп для UI: `isDuplicateForDisplay()` + `detectionHistory` (окно 60 с).

## Веб-API (HTTP :80, события :81 WebSocket)

`GET/POST /api/keys`, `/api/keys/learn|stop|status|delete|update`, `/api/phones[...]`, `/api/wifi/scan|connect`, `/api/frequency[/set]`, `/api/cc1101/config`, `/api/gate/trigger`. WebSocket события: `log`, `key_received`, `key_added`, `wifi_status`. Статика отдаётся из SPIFFS (`/`, `/static/...`).

Wi-Fi: всегда поднимается AP `SmartGate-Config` / пароль `12345678` (`192.168.4.1`), плюс mDNS `smartgate.local`. При наличии сохранённых креды пытается подключиться к роутеру.

## Конфигурация радио (Fixed Scan, как Flipper)

В `CC1101Manager::init()`: ASK/OOK, битрейт 3.79 kbps, RX BW 58 кГц, девиация 5.2 кГц, мощность 10 dBm, частота по умолчанию 433.92 МГц. Частота меняется рантайм через `/api/frequency/set` (300–928 МГц).

## Известные ограничения / TODO

- **Rolling code не поддержан**: Keeloq/Nice FloR сравниваются как статические коды — настоящий динамический код так не отловить.
- **Только OOK/ASK реально работает.** `setModulation()` для FSK/MSK/GFSK — заглушка → часть 868-МГц брелоков не возьмётся.
- **Только приём (RX).** TX (эмуляция/отправка ключа) не реализован.
- README рассинхронизирован с кодом; `main.cpp` стоило бы разнести по модулям.
- `data/` — артефакт сборки фронта; менять UI нужно в `smart-gate-frontend/` и пересобирать (`./upload.sh` делает это сам).

## Стиль

Комментарии и логи — на русском, как в существующем коде. Не плодить временные костыли (см. глобальные инструкции пользователя). Перед «готово» — проверять реальным запуском/логами, а не только сборкой.
