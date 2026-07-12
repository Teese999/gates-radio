#include <Arduino.h>
#include "GSMManager.h"
#include "infrastructure/Logger.h"

namespace GSMManager {
  // UART2: аппаратный порт ESP32 (пины задаются в begin)
  static HardwareSerial gsmSerial(2);

  // SIM800L бывает и с автобаудом, и с жёстко прошитой скоростью (часто 115200).
  // При поиске модуля перебираем скорости: 2 пробы "AT" на каждой, по кругу.
  static const uint32_t BAUDS[] = {9600, 115200, 57600, 38400, 19200};
  static const int BAUD_COUNT = sizeof(BAUDS) / sizeof(BAUDS[0]);
  static int baudIndex = 0;
  static int probesAtThisBaud = 0;

  static TrustedCheckFn trustedCheckFn = nullptr;
  static GateOpenFn gateOpenFn = nullptr;

  // --- State machine инициализации ---
  // SIM800L стартует 3-10 с и может быть вообще не подключён, поэтому
  // конфигурация идёт асинхронно: команда → ждём "OK" в общем парсере строк.
  enum class State { OFFLINE, PROBING, CONFIGURING, READY };
  static State state = State::OFFLINE;

  static const unsigned long PROBE_RETRY_MS = 5000; // повтор "AT", пока модуль молчит
  static const unsigned long CMD_TIMEOUT_MS = 2000;  // ожидание "OK" на команду
  static unsigned long lastCmdSentAt = 0;

  // Команды конфигурации (по очереди, каждая ждёт OK):
  // ATE0        - выключить эхо
  // AT+CLIP=1   - АОН: номер звонящего в URC +CLIP
  // AT+CMGF=1   - SMS в текстовом режиме
  // AT+CNMI=2,2,0,0,0 - входящие SMS сразу в UART (+CMT), не копятся на SIM
  static const char* CONFIG_CMDS[] = { "ATE0", "AT+CLIP=1", "AT+CMGF=1", "AT+CNMI=2,2,0,0,0" };
  static const int CONFIG_CMD_COUNT = sizeof(CONFIG_CMDS) / sizeof(CONFIG_CMDS[0]);
  static int configCmdIndex = 0;

  // --- Парсер строк от модуля ---
  static String lineBuf;

  // Ожидание тела SMS: после "+CMT: ..." следующая строка — текст сообщения
  static bool awaitingSmsBody = false;
  static String smsSender;

  // Дедупликация: +CLIP повторяется с каждым RING (~раз в 4-5 с), пока звонок
  // не сброшен — реагируем на один и тот же номер не чаще раза в 10 с
  static String lastCallNumber;
  static unsigned long lastCallHandledAt = 0;
  static const unsigned long CALL_DEDUP_MS = 10000;

  // Страховка: RING без +CLIP (оператор не передал номер) — сброс через таймаут,
  // иначе безномерной звонок трезвонил бы до таймаута оператора
  static bool ringPending = false;
  static unsigned long firstRingAt = 0;
  static const unsigned long RING_NO_CLIP_TIMEOUT_MS = 8000;

  static void sendCmd(const char* cmd) {
    gsmSerial.print(cmd);
    gsmSerial.print("\r\n");
    lastCmdSentAt = millis();
  }

  // Извлечение номера из URC вида: +CLIP: "+79991234567",145,...
  // или +CMT: "+79991234567","","24/07/05,12:00:00+12"
  static String extractQuotedNumber(const String& line) {
    int q1 = line.indexOf('"');
    if (q1 < 0) return "";
    int q2 = line.indexOf('"', q1 + 1);
    if (q2 < 0) return "";
    return line.substring(q1 + 1, q2);
  }

  static void handleIncomingCall(const String& number) {
    // Повторный +CLIP того же звонка — игнорируем
    if (number == lastCallNumber && millis() - lastCallHandledAt < CALL_DEDUP_MS) {
      return;
    }
    lastCallNumber = number;
    lastCallHandledAt = millis();

    // Линию освобождаем в любом случае (не отвечаем — звонок бесплатный для звонящего)
    sendCmd("ATH");

    if (trustedCheckFn && trustedCheckFn(number, true)) {
      Logger::success("[GSM] Звонок с доверенного номера: " + number);
      if (gateOpenFn) gateOpenFn("звонок " + number);
    } else {
      Logger::warning("[GSM] Звонок с неизвестного номера (сброшен): " + number);
    }
  }

  static void handleIncomingSms(const String& sender, const String& text) {
    if (trustedCheckFn && trustedCheckFn(sender, false)) {
      Logger::success("[GSM] SMS с доверенного номера " + sender + ": " + text);
      if (gateOpenFn) gateOpenFn("SMS " + sender);
    } else {
      Logger::warning("[GSM] SMS с неизвестного номера " + sender + ": " + text);
    }
  }

  // Мусор на линии (наводки, обрыв провода, неверная скорость) не печатаем
  // построчно — иначе он топит Serial и лог нечитаем. Раз в 5 с — сводка.
  static unsigned long junkWindowStart = 0;
  static uint32_t junkLineCount = 0;

  static bool isMostlyPrintable(const String& line) {
    int printable = 0;
    for (unsigned int i = 0; i < line.length(); i++) {
      char c = line[i];
      if (c >= 32 && c < 127) printable++;
    }
    return printable * 10 >= (int)line.length() * 7; // >= 70% печатаемых
  }

  static void processLine(const String& line) {
    if (line.length() == 0) return;

    if (!isMostlyPrintable(line)) {
      junkLineCount++;
      if (millis() - junkWindowStart > 5000) {
        Serial.printf("[GSM] Шум на линии: %u строк за 5с (проверьте провод TXD-GPIO16 и землю)\n",
                      junkLineCount);
        junkWindowStart = millis();
        junkLineCount = 0;
      }
      return;
    }

    Serial.println("[GSM] << " + line);

    // Тело SMS — строка, следующая за "+CMT:". Триггерим по факту SMS с
    // доверенного номера, содержимое не проверяем; многострочные SMS дают
    // лишние строки — они уйдут в ветку "неизвестный URC" и будут проигнорированы.
    if (awaitingSmsBody) {
      awaitingSmsBody = false;
      handleIncomingSms(smsSender, line);
      return;
    }

    // Ответ "OK" двигает state machine конфигурации
    if (line == "OK") {
      if (state == State::PROBING) {
        Logger::info("[GSM] Модуль ответил на скорости " + String(BAUDS[baudIndex]));
        state = State::CONFIGURING;
        configCmdIndex = 0;
        sendCmd(CONFIG_CMDS[configCmdIndex]);
      } else if (state == State::CONFIGURING) {
        configCmdIndex++;
        if (configCmdIndex >= CONFIG_CMD_COUNT) {
          state = State::READY;
          Logger::success("[GSM] SIM800L готов: звонки и SMS отслеживаются");
        } else {
          sendCmd(CONFIG_CMDS[configCmdIndex]);
        }
      }
      return;
    }

    if (line == "RING") {
      // Ждём +CLIP с номером (приходит следом); если не придёт — сброс по таймауту
      if (!ringPending) {
        ringPending = true;
        firstRingAt = millis();
      }
      return;
    }

    if (line.startsWith("+CLIP:")) {
      ringPending = false;
      String number = extractQuotedNumber(line);
      if (number.length() > 0) {
        handleIncomingCall(number);
      }
      return;
    }

    if (line.startsWith("+CMT:")) {
      smsSender = extractQuotedNumber(line);
      awaitingSmsBody = smsSender.length() > 0;
      return;
    }

    // RING, Call Ready, SMS Ready, эхо команд и прочие URC — не интересны
  }

  void init(int rxPin, int txPin, TrustedCheckFn trustedCheck, GateOpenFn gateOpen) {
    trustedCheckFn = trustedCheck;
    gateOpenFn = gateOpen;

    gsmSerial.begin(BAUDS[baudIndex], SERIAL_8N1, rxPin, txPin);

    state = State::PROBING;
    sendCmd("AT");
    Logger::info("[GSM] Поиск SIM800L на UART2 (RX=" + String(rxPin) + ", TX=" + String(txPin) + ")...");
  }

  void handleGSM() {
    // Чтение доступных байт без блокировки
    while (gsmSerial.available()) {
      char c = (char)gsmSerial.read();
      if (c == '\n') {
        lineBuf.trim();
        processLine(lineBuf);
        lineBuf = "";
      } else if (c != '\r') {
        lineBuf += c;
        // Защита от мусора при отсутствии \n (наводки на висящем RX)
        if (lineBuf.length() > 256) {
          lineBuf = "";
        }
      }
    }

    // Таймауты state machine
    unsigned long now = millis();
    switch (state) {
      case State::PROBING:
        // Модуль молчит (грузится, не подключён или другая скорость) — пробуем
        // снова; после 2 проб на текущей скорости переключаемся на следующую
        if (now - lastCmdSentAt > PROBE_RETRY_MS) {
          probesAtThisBaud++;
          if (probesAtThisBaud >= 2) {
            probesAtThisBaud = 0;
            baudIndex = (baudIndex + 1) % BAUD_COUNT;
            gsmSerial.updateBaudRate(BAUDS[baudIndex]);
            lineBuf = "";
            Serial.printf("[GSM] Пробуем скорость %lu\n", (unsigned long)BAUDS[baudIndex]);
          }
          sendCmd("AT");
        }
        break;
      case State::CONFIGURING:
        // Команда без ответа — начинаем заново с пробы
        if (now - lastCmdSentAt > CMD_TIMEOUT_MS) {
          Logger::warning("[GSM] SIM800L не ответил на конфигурацию, повтор...");
          state = State::PROBING;
          lastCmdSentAt = now - PROBE_RETRY_MS; // немедленная повторная проба
        }
        break;
      case State::READY:
        // Звонок без определившегося номера — сбрасываем по таймауту
        if (ringPending && now - firstRingAt > RING_NO_CLIP_TIMEOUT_MS) {
          ringPending = false;
          sendCmd("ATH");
          Logger::warning("[GSM] Звонок без определения номера — сброшен");
        }
        break;
      default:
        break;
    }
  }

  bool isReady() {
    return state == State::READY;
  }
}
