#include <Arduino.h>
#include <ArduinoJson.h>
#include <GxEPD2_BW.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>
#include <time.h>

/*
 * BVG tram departure display for the Elecrow CrowPanel 2.13-inch e-paper
 * Board: DIE01021S V1.2 / ESP32-S3 / SSD1680-family display
 *
 * Wi-Fi password:
 *   data/wifi-password.txt
 *
 * Upload filesystem once after creating/changing the password file:
 *   pio run -t uploadfs
 *
 * Then upload the application:
 *   pio run -t upload
 */

// -----------------------------------------------------------------------------
// CrowPanel e-paper wiring (verified for DIE01021S V1.2)
// -----------------------------------------------------------------------------
constexpr int EPD_POWER = 7;
constexpr int EPD_BUSY  = 9;
constexpr int EPD_RST   = 10;
constexpr int EPD_MOSI  = 11;
constexpr int EPD_SCK   = 12;
constexpr int EPD_DC    = 13;
constexpr int EPD_CS    = 14;

GxEPD2_BW<GxEPD2_213_GDEY0213B74, GxEPD2_213_GDEY0213B74::HEIGHT> display(
    GxEPD2_213_GDEY0213B74(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// -----------------------------------------------------------------------------
// Application configuration
// -----------------------------------------------------------------------------
// Deliberately fixed by the original requirement; only the password is secret.
constexpr char WIFI_SSID[] = "birnensaft";
constexpr char PASSWORD_FILE[] = "/wifi-password.txt";
constexpr char API_BASE_URL[] = "https://v6.bvg.transport.rest";
constexpr char STOP_SEARCH_TERM[] = "Erich-Baron-Weg";
constexpr char LANGUAGE[] = "de";
constexpr char BERLIN_TZ[] = "CET-1CEST,M3.5.0,M10.5.0/3";

constexpr char TRAM_PRODUCTS[] =
    "tram=true&suburban=false&subway=false&bus=false"
    "&ferry=false&express=false&regional=false";
constexpr char COMMON_API_PARAMS[] = "&pretty=false";
constexpr char DEPARTURE_API_PARAMS[] =
    "&remarks=false&linesOfStops=false";

constexpr char TXT_BOOT_HEADER[] = "BVG Abfahrten";
constexpr char TXT_NEXT_TRAMS[] = "naechste Trams";
constexpr char TXT_DATA_AS_OF[] = "Stand ";
constexpr char TXT_NO_DEPARTURES[] = "Keine Abfahrt";
constexpr char TXT_RETRYING[] = "Daten werden erneut geladen.";
constexpr char TXT_ERROR_PREFIX[] = "BVG: ";
constexpr char TXT_STALE_DATA[] = " - alte Daten";
constexpr char TXT_MINUTES[] = " min";
constexpr char TXT_UNKNOWN_DIRECTION[] = "Unbekannt";
constexpr char TXT_BVG_ERROR[] = "BVG Fehler";
constexpr char TXT_CONFIG_ERROR[] = "Konfigurationsfehler";
constexpr char TXT_WIFI_ERROR[] = "WiFi Fehler";
constexpr char TXT_TIME_ERROR[] = "Zeit Fehler";
constexpr char TXT_BOOT_INFO_PREFIX[] = "> ";
constexpr char TXT_BOOT_ERROR_PREFIX[] = "ERR ";

constexpr char WIFI_STATUS_IDLE[] = "idle";
constexpr char WIFI_STATUS_NO_SSID[] = "SSID not found";
constexpr char WIFI_STATUS_SCAN_COMPLETE[] = "scan complete";
constexpr char WIFI_STATUS_CONNECTED[] = "connected";
constexpr char WIFI_STATUS_AUTH_FAILED[] = "auth failed";
constexpr char WIFI_STATUS_CONNECTION_LOST[] = "connection lost";
constexpr char WIFI_STATUS_DISCONNECTED[] = "disconnected";

constexpr unsigned long REFRESH_MS = 60UL * 1000UL;
constexpr unsigned long WIFI_TIMEOUT_MS = 30000UL;
constexpr unsigned long NTP_TIMEOUT_MS = 25000UL;
constexpr unsigned long HTTP_TIMEOUT_MS = 15000UL;
constexpr unsigned long ERROR_RETRY_MS = 15UL * 1000UL;
constexpr int HTTP_MAX_ATTEMPTS = 3;
constexpr unsigned long HTTP_BACKOFF_MS[HTTP_MAX_ATTEMPTS] = {
    0UL, 2000UL, 6000UL
};

constexpr int MAX_DISPLAY_DEPARTURES = 2;
constexpr int PARTIAL_REFRESHES_BEFORE_FULL = 10;
constexpr int BOOT_LOG_LINES = 11;
constexpr size_t BOOT_LOG_CHARS = 40;

// Landscape framebuffer is 250 x 122 after setRotation(1).
// Coordinates below are separated into non-overlapping vertical bands:
// header 0..20, departure 1 24..67, departure 2 72..111, footer 114..121.
constexpr int16_t DISPLAY_X_CORRECTION = 0;
constexpr int16_t DISPLAY_MARGIN_X = 4;
constexpr int16_t DISPLAY_RIGHT_X = 246;
constexpr int16_t HEADER_STOP_Y = 5;
constexpr int16_t HEADER_TIME_Y = 2;
constexpr int16_t HEADER_RULE_Y = 21;
constexpr int16_t FIRST_DEPARTURE_Y = 25;
constexpr int16_t DEPARTURE_ROW_HEIGHT = 48;
constexpr int16_t DESTINATION_Y_OFFSET = 21;
constexpr int16_t DEPARTURE_DIVIDER_Y = 69;
constexpr int16_t DISPLAY_FOOTER_Y = 114;
constexpr uint8_t HEADER_STOP_TEXT_SIZE = 1;
constexpr uint8_t HEADER_TIME_TEXT_SIZE = 2;
constexpr uint8_t DEPARTURE_TEXT_SIZE = 2;
constexpr uint8_t FOOTER_TEXT_SIZE = 1;

String wifiPassword;
String stopId;
String stopDisplayName = STOP_SEARCH_TERM;
unsigned long lastRefresh = 0;
unsigned long currentRefreshInterval = REFRESH_MS;
int partialRefreshCount = 0;
bool displayReady = false;
bool haveDepartureData = false;
String lastError;

struct Departure {
  String line;
  String direction;
  time_t when = 0;
  int delayMinutes = 0;
};

Departure cachedDepartures[MAX_DISPLAY_DEPARTURES];
int cachedDepartureCount = 0;
time_t lastSuccessfulUpdate = 0;

String bootLog[BOOT_LOG_LINES];
int bootLogCount = 0;

// -----------------------------------------------------------------------------
// Text and display helpers
// -----------------------------------------------------------------------------
String asciiGerman(String text) {
  text.replace("ä", "ae");
  text.replace("ö", "oe");
  text.replace("ü", "ue");
  text.replace("Ä", "Ae");
  text.replace("Ö", "Oe");
  text.replace("Ü", "Ue");
  text.replace("ß", "ss");
  return text;
}

String clipped(String text, size_t maxChars) {
  text = asciiGerman(text);
  if (text.length() <= maxChars) return text;
  if (maxChars <= 3) return text.substring(0, maxChars);
  return text.substring(0, maxChars - 3) + "...";
}

int16_t displayX(int16_t x) {
  return x + DISPLAY_X_CORRECTION;
}

uint16_t textWidth(const String& text, uint8_t textSize) {
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t width = 0;
  uint16_t height = 0;

  display.setTextSize(textSize);
  display.getTextBounds(text.c_str(), 0, 0, &x1, &y1, &width, &height);
  return width;
}

String clippedToWidth(String text, uint16_t maxWidth, uint8_t textSize) {
  text = asciiGerman(text);
  if (textWidth(text, textSize) <= maxWidth) return text;

  const String ellipsis = "...";
  while (!text.isEmpty() &&
         textWidth(text + ellipsis, textSize) > maxWidth) {
    text.remove(text.length() - 1);
  }

  return text + ellipsis;
}

void drawHorizontalRule(int16_t y) {
  display.drawFastHLine(
      displayX(DISPLAY_MARGIN_X),
      y,
      display.width() - (2 * DISPLAY_MARGIN_X),
      GxEPD_BLACK);
}

void drawFullWidthLine(int16_t y) {
  display.drawFastHLine(
      displayX(0),
      y,
      display.width(),
      GxEPD_BLACK);
}

void drawRightAligned(const String& text, int16_t rightX,
                      int16_t y, uint8_t textSize) {
  display.setTextSize(textSize);
  const uint16_t width = textWidth(text, textSize);
  display.setCursor(displayX(rightX - static_cast<int16_t>(width)), y);
  display.print(text);
}

String shortDirection(String direction) {
  direction = asciiGerman(direction);

  const int viaPosition = direction.indexOf(" via ");
  if (viaPosition >= 0) {
    direction = direction.substring(0, viaPosition);
  }

  direction.replace("Betriebshof Marzahn", "Btf. Marzahn");
  direction.replace("Betriebshof Koepenick", "Btf. Koepenick");

  const uint16_t maxWidth =
      display.width() - (2 * DISPLAY_MARGIN_X);
  return clippedToWidth(direction, maxWidth, DEPARTURE_TEXT_SIZE);
}

String hhmm(time_t value) {
  if (value <= 0) return "--:--";
  struct tm local {};
  localtime_r(&value, &local);
  char output[6];
  strftime(output, sizeof(output), "%H:%M", &local);
  return String(output);
}

String resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "Power-on";
    case ESP_RST_EXT:       return "External reset";
    case ESP_RST_SW:        return "Software reset";
    case ESP_RST_PANIC:     return "Panic/crash";
    case ESP_RST_INT_WDT:   return "Interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "Task watchdog";
    case ESP_RST_WDT:       return "Other watchdog";
    case ESP_RST_DEEPSLEEP: return "Deep-sleep wake";
    case ESP_RST_BROWNOUT:  return "Brownout";
    case ESP_RST_SDIO:      return "SDIO reset";
    default:                return "Unknown reset";
  }
}

void drawBootLog(bool fullRefresh = false) {
  if (!displayReady) return;

  if (fullRefresh) {
    display.setFullWindow();
  } else {
    display.setPartialWindow(0, 0, display.width(), display.height());
  }

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setTextWrap(false);

    display.setTextSize(1);
    display.setCursor(displayX(4), 10);
    display.print(TXT_BOOT_HEADER);
    drawFullWidthLine(14);

    int y = 24;
    for (int i = 0; i < bootLogCount && i < BOOT_LOG_LINES; ++i) {
      display.setCursor(displayX(4), y);
      display.print(clipped(bootLog[i], BOOT_LOG_CHARS));
      y += 9;
    }
  } while (display.nextPage());
}

void appendBootLog(const String& message, bool isError = false) {
  const String prefix = isError
      ? TXT_BOOT_ERROR_PREFIX
      : TXT_BOOT_INFO_PREFIX;
  const String line = prefix + asciiGerman(message);

  if (bootLogCount < BOOT_LOG_LINES) {
    bootLog[bootLogCount++] = line;
  } else {
    for (int i = 1; i < BOOT_LOG_LINES; ++i) {
      bootLog[i - 1] = bootLog[i];
    }
    bootLog[BOOT_LOG_LINES - 1] = line;
  }

  if (isError) {
    Serial.print("ERROR: ");
  }
  Serial.println(message);
  drawBootLog(false);
}

void initializeDisplay() {
  Serial.println("DISPLAY: power on");
  pinMode(EPD_POWER, OUTPUT);
  digitalWrite(EPD_POWER, HIGH);
  delay(150);

  Serial.println("DISPLAY: SPI init");
  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  display.epd2.selectSPI(SPI, SPISettings(4000000UL, MSBFIRST, SPI_MODE0));

  Serial.println("DISPLAY: driver init");
  display.init(115200);
  display.setRotation(1);  // 250 x 122 landscape
  display.setTextColor(GxEPD_BLACK);
  display.setTextWrap(false);
  displayReady = true;

  bootLogCount = 0;
  bootLog[bootLogCount++] = "> Display ready";
  bootLog[bootLogCount++] = "> Reset: " + resetReasonText();
  drawBootLog(true);
  Serial.println("DISPLAY: ready");
}

void drawCenteredMessage(const String& title, const String& detail,
                         bool fullRefresh = false) {
  if (!displayReady) return;

  if (fullRefresh) {
    display.setFullWindow();
  } else {
    display.setPartialWindow(0, 0, display.width(), display.height());
  }

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setTextWrap(false);

    display.setTextSize(2);
    display.setCursor(displayX(8), 35);
    display.print(clipped(title, 19));

    display.setTextSize(1);
    display.setCursor(displayX(8), 62);
    display.print(clipped(detail, 38));
  } while (display.nextPage());
}

void drawDepartureScreen(bool forceFullRefresh = false) {
  if (!displayReady) return;

  const bool fullRefresh =
      forceFullRefresh || partialRefreshCount >= PARTIAL_REFRESHES_BEFORE_FULL;

  if (fullRefresh) {
    display.setFullWindow();
    partialRefreshCount = 0;
  } else {
    display.setPartialWindow(0, 0, display.width(), display.height());
    ++partialRefreshCount;
  }

  const time_t now = time(nullptr);

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setTextWrap(false);

    // Header occupies y=0..20. Current time is right-aligned, leaving the
    // remaining width for the stop name. No header text crosses the rule.
    const String currentTime = hhmm(now);
    const uint16_t currentTimeWidth =
        textWidth(currentTime, HEADER_TIME_TEXT_SIZE);
    const int16_t currentTimeX =
        DISPLAY_RIGHT_X - static_cast<int16_t>(currentTimeWidth);
    const uint16_t stopNameMaxWidth =
        currentTimeX - DISPLAY_MARGIN_X - 8;

    display.setTextSize(HEADER_STOP_TEXT_SIZE);
    display.setCursor(displayX(DISPLAY_MARGIN_X), HEADER_STOP_Y);
    display.print(clippedToWidth(
        stopDisplayName, stopNameMaxWidth, HEADER_STOP_TEXT_SIZE));

    display.setTextSize(HEADER_TIME_TEXT_SIZE);
    display.setCursor(displayX(currentTimeX), HEADER_TIME_Y);
    display.print(currentTime);

    drawHorizontalRule(HEADER_RULE_Y);

    if (cachedDepartureCount <= 0) {
      display.setTextSize(2);
      display.setCursor(displayX(DISPLAY_MARGIN_X), 43);
      display.print(TXT_NO_DEPARTURES);

      display.setTextSize(1);
      display.setCursor(displayX(DISPLAY_MARGIN_X), 68);
      display.print(clippedToWidth(
          TXT_RETRYING,
          display.width() - (2 * DISPLAY_MARGIN_X),
          1));
    } else {
      for (int i = 0; i < MAX_DISPLAY_DEPARTURES; ++i) {
        const int16_t rowY =
            FIRST_DEPARTURE_Y + i * DEPARTURE_ROW_HEIGHT;

        if (i >= cachedDepartureCount) {
          display.setTextSize(DEPARTURE_TEXT_SIZE);
          display.setCursor(displayX(DISPLAY_MARGIN_X), rowY);
          display.print("--:--");
          continue;
        }

        long seconds = cachedDepartures[i].when - now;
        if (seconds < 0) seconds = 0;
        const int minutes = static_cast<int>((seconds + 59) / 60);

        const String minutesText = String(minutes) + TXT_MINUTES;
        const uint16_t minutesWidth =
            textWidth(minutesText, DEPARTURE_TEXT_SIZE);
        const int16_t minutesX =
            DISPLAY_RIGHT_X - static_cast<int16_t>(minutesWidth);

        String delayText;
        if (cachedDepartures[i].delayMinutes >= 0) {
          delayText = "+";
        }
        delayText += String(cachedDepartures[i].delayMinutes);

        const String departureTime = hhmm(cachedDepartures[i].when);
        const String line = cachedDepartures[i].line;
        String mainText =
            departureTime + " " + delayText + "  " + line;

        // Reserve a fixed gap before the right-aligned countdown. If an
        // unusually long delay or line name does not fit, degrade gracefully
        // without ever drawing underneath the countdown.
        const uint16_t mainMaxWidth =
            minutesX - DISPLAY_MARGIN_X - 8;
        if (textWidth(mainText, DEPARTURE_TEXT_SIZE) > mainMaxWidth) {
          mainText = departureTime + " " + delayText + " " + line;
        }
        if (textWidth(mainText, DEPARTURE_TEXT_SIZE) > mainMaxWidth) {
          mainText = departureTime + " " + line;
        }
        mainText = clippedToWidth(
            mainText, mainMaxWidth, DEPARTURE_TEXT_SIZE);

        display.setTextSize(DEPARTURE_TEXT_SIZE);
        display.setCursor(displayX(DISPLAY_MARGIN_X), rowY);
        display.print(mainText);

        drawRightAligned(
            minutesText,
            DISPLAY_RIGHT_X,
            rowY,
            DEPARTURE_TEXT_SIZE);

        display.setTextSize(DEPARTURE_TEXT_SIZE);
        display.setCursor(
            displayX(DISPLAY_MARGIN_X),
            rowY + DESTINATION_Y_OFFSET);
        display.print(shortDirection(cachedDepartures[i].direction));

        if (i == 0) {
          drawHorizontalRule(DEPARTURE_DIVIDER_Y);
        }
      }
    }

    // Only failures use the final 8-pixel band. The second destination ends
    // at y=110, leaving a clean gap before this footer at y=114.
    if (!lastError.isEmpty()) {
      display.setTextSize(FOOTER_TEXT_SIZE);
      display.setCursor(
          displayX(DISPLAY_MARGIN_X),
          DISPLAY_FOOTER_Y);
      display.print(clippedToWidth(
          String("! ") + lastError + TXT_STALE_DATA,
          display.width() - (2 * DISPLAY_MARGIN_X),
          FOOTER_TEXT_SIZE));
    }
  } while (display.nextPage());
}

// -----------------------------------------------------------------------------
// Filesystem, Wi-Fi and time
// -----------------------------------------------------------------------------
bool readPassword() {
  appendBootLog("Mounting LittleFS");
  if (!LittleFS.begin(false)) {
    lastError = "LittleFS mount failed";
    appendBootLog(lastError, true);
    return false;
  }

  appendBootLog("Reading password file");
  File file = LittleFS.open(PASSWORD_FILE, "r");
  if (!file) {
    lastError = "Missing wifi-password.txt";
    appendBootLog(lastError, true);
    return false;
  }

  wifiPassword = file.readString();
  file.close();
  wifiPassword.trim();

  if (wifiPassword.isEmpty()) {
    lastError = "Password file is empty";
    appendBootLog(lastError, true);
    return false;
  }

  appendBootLog("Password loaded (" + String(wifiPassword.length()) + " chars)");
  return true;
}

String wifiStatusText(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS:     return WIFI_STATUS_IDLE;
    case WL_NO_SSID_AVAIL:   return WIFI_STATUS_NO_SSID;
    case WL_SCAN_COMPLETED:  return WIFI_STATUS_SCAN_COMPLETE;
    case WL_CONNECTED:       return WIFI_STATUS_CONNECTED;
    case WL_CONNECT_FAILED:  return WIFI_STATUS_AUTH_FAILED;
    case WL_CONNECTION_LOST: return WIFI_STATUS_CONNECTION_LOST;
    case WL_DISCONNECTED:    return WIFI_STATUS_DISCONNECTED;
    default:                 return "status " + String(static_cast<int>(status));
  }
}

bool connectWifi(unsigned long timeoutMs = WIFI_TIMEOUT_MS) {
  if (WiFi.status() == WL_CONNECTED) return true;

  appendBootLog("WiFi: connecting to " + String(WIFI_SSID));
  Serial.printf("Connecting to %s", WIFI_SSID);

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.disconnect(false, false);
  delay(250);
  WiFi.begin(WIFI_SSID, wifiPassword.c_str());

  const unsigned long started = millis();
  unsigned long lastDisplayUpdate = 0;

  while (WiFi.status() != WL_CONNECTED && millis() - started < timeoutMs) {
    delay(500);
    Serial.print('.');

    if (millis() - lastDisplayUpdate >= 5000) {
      lastDisplayUpdate = millis();
      const unsigned long elapsed = (millis() - started) / 1000;
      appendBootLog("WiFi: " + wifiStatusText(WiFi.status()) +
                    " (" + String(elapsed) + "s)");
    }
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    lastError = "WiFi " + wifiStatusText(WiFi.status());
    appendBootLog(lastError, true);
    return false;
  }

  lastError = "";
  appendBootLog("WiFi OK: " + WiFi.localIP().toString());
  Serial.printf("Connected: %s\n", WiFi.localIP().toString().c_str());
  return true;
}

bool synchronizeClock(unsigned long timeoutMs = NTP_TIMEOUT_MS) {
  appendBootLog("NTP: synchronizing");
  Serial.print("Synchronizing time");

  configTzTime(BERLIN_TZ, "pool.ntp.org", "time.cloudflare.com");

  const unsigned long started = millis();
  struct tm value {};
  unsigned long lastDisplayUpdate = 0;

  while (millis() - started < timeoutMs) {
    if (getLocalTime(&value, 1000)) {
      Serial.println();
      lastError = "";
      appendBootLog("NTP OK: " + hhmm(time(nullptr)));
      return true;
    }

    Serial.print('.');
    if (millis() - lastDisplayUpdate >= 5000) {
      lastDisplayUpdate = millis();
      appendBootLog("NTP: waiting " +
                    String((millis() - started) / 1000) + "s");
    }
  }

  Serial.println();
  lastError = "NTP timeout";
  appendBootLog(lastError, true);
  return false;
}

// -----------------------------------------------------------------------------
// BVG API
// -----------------------------------------------------------------------------
bool isTransientHttpFailure(int status) {
  return status < 0 ||
         status == HTTP_CODE_REQUEST_TIMEOUT ||
         status == 425 ||
         status == 429 ||
         status == HTTP_CODE_INTERNAL_SERVER_ERROR ||
         status == HTTP_CODE_BAD_GATEWAY ||
         status == HTTP_CODE_SERVICE_UNAVAILABLE ||
         status == HTTP_CODE_GATEWAY_TIMEOUT;
}

void reportNetworkOperation(const String& message) {
  if (haveDepartureData) {
    Serial.println(message);
  } else {
    appendBootLog(message);
  }
}

void reportRetry(const String& message) {
  Serial.println(message);
  if (!haveDepartureData) {
    appendBootLog(message);
  }
}

bool getJson(const String& url, JsonDocument& result,
             const JsonDocument& filter, const String& operation) {
  if (WiFi.status() != WL_CONNECTED && !connectWifi()) {
    return false;
  }

  reportNetworkOperation(operation);

  for (int attempt = 0; attempt < HTTP_MAX_ATTEMPTS; ++attempt) {
    if (HTTP_BACKOFF_MS[attempt] > 0) {
      const unsigned long waitMs = HTTP_BACKOFF_MS[attempt];
      reportRetry("BVG retry " + String(attempt + 1) + "/" +
                  String(HTTP_MAX_ATTEMPTS) + " in " +
                  String(waitMs / 1000) + "s");
      delay(waitMs);
    }

    WiFiClientSecure client;
    client.setInsecure();  // Prototype: validate the CA certificate in production.

    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if (!http.begin(client, url)) {
      lastError = "HTTPS init failed";
      if (attempt + 1 < HTTP_MAX_ATTEMPTS) continue;
      Serial.println("ERROR: " + lastError);
      if (!haveDepartureData) appendBootLog(lastError, true);
      return false;
    }

    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", "ESP32-BVG-Tram/3.0");

    const int status = http.GET();

    if (status == HTTP_CODE_OK) {
      const DeserializationError error = deserializeJson(
          result, http.getStream(), DeserializationOption::Filter(filter));
      http.end();

      if (error) {
        lastError = "JSON " + String(error.c_str());
        Serial.println("ERROR: " + lastError);
        if (!haveDepartureData) appendBootLog(lastError, true);
        return false;
      }

      lastError = "";
      return true;
    }

    http.end();

    Serial.printf("ERROR: HTTP %d for %s (attempt %d/%d)\n",
                  status, url.c_str(), attempt + 1, HTTP_MAX_ATTEMPTS);

    const bool canRetry =
        isTransientHttpFailure(status) && attempt + 1 < HTTP_MAX_ATTEMPTS;

    if (canRetry) {
      continue;
    }

    lastError = "HTTP " + String(status);
    if (!haveDepartureData) {
      appendBootLog(lastError, true);
    }
    return false;
  }

  lastError = "HTTP retry exhausted";
  return false;
}

const JsonDocument& stopLookupJsonFilter() {
  static JsonDocument filter;
  static bool initialized = false;

  if (!initialized) {
    filter[0]["type"] = true;
    filter[0]["id"] = true;
    filter[0]["name"] = true;
    filter[0]["products"]["tram"] = true;
    initialized = true;
  }

  return filter;
}

const JsonDocument& departureJsonFilter() {
  static JsonDocument filter;
  static bool initialized = false;

  if (!initialized) {
    filter["departures"][0]["direction"] = true;
    filter["departures"][0]["line"]["name"] = true;
    filter["departures"][0]["line"]["product"] = true;
    filter["departures"][0]["when"] = true;
    filter["departures"][0]["plannedWhen"] = true;
    filter["departures"][0]["delay"] = true;
    filter["departures"][0]["cancelled"] = true;
    initialized = true;
  }

  return filter;
}

bool findStopId() {
  const String url = String(API_BASE_URL) +
      "/locations?query=" + String(STOP_SEARCH_TERM) +
      "&poi=false&addresses=false&results=8&language=" +
      LANGUAGE + COMMON_API_PARAMS;

  JsonDocument result;
  if (!getJson(url, result, stopLookupJsonFilter(),
               "BVG: finding stop")) {
    return false;
  }

  for (JsonObject item : result.as<JsonArray>()) {
    const String name = item["name"] | "";
    const String type = item["type"] | "";
    const bool tram = item["products"]["tram"] | false;

    if (type == "stop" && tram && name.indexOf(STOP_SEARCH_TERM) >= 0) {
      stopId = String(item["id"] | "");
      stopDisplayName = name;
      Serial.printf("Stop: %s, ID: %s\n", name.c_str(), stopId.c_str());
      appendBootLog("Stop OK: " + stopId);
      return !stopId.isEmpty();
    }
  }

  lastError = "Stop not found";
  appendBootLog(lastError, true);
  return false;
}

// Number of days since 1970-01-01. This Gregorian conversion avoids
// depending on the process-local timezone when the API supplies an offset.
int64_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra =
      static_cast<unsigned>(year - era * 400);
  const unsigned adjustedMonth =
      month > 2 ? month - 3 : month + 9;
  const unsigned dayOfYear =
      (153 * adjustedMonth + 2) / 5 + day - 1;
  const unsigned dayOfEra =
      yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 +
      dayOfYear;
  return static_cast<int64_t>(era) * 146097 +
         static_cast<int64_t>(dayOfEra) - 719468;
}

int parseDigits(const char* text, size_t start, size_t count) {
  int value = 0;
  for (size_t i = 0; i < count; ++i) {
    const char character = text[start + i];
    if (character < '0' || character > '9') return -1;
    value = value * 10 + (character - '0');
  }
  return value;
}

// Parses timestamps such as 2026-07-26T23:18:00+02:00 or ...Z.
// An explicit API offset is converted to UTC instead of being ignored.
time_t parseApiTime(const char* text) {
  if (!text || strlen(text) < 19) return 0;

  const int year = parseDigits(text, 0, 4);
  const int month = parseDigits(text, 5, 2);
  const int day = parseDigits(text, 8, 2);
  const int hour = parseDigits(text, 11, 2);
  const int minute = parseDigits(text, 14, 2);
  const int second = parseDigits(text, 17, 2);

  if (year < 1970 || month < 1 || month > 12 ||
      day < 1 || day > 31 || hour < 0 || hour > 23 ||
      minute < 0 || minute > 59 || second < 0 || second > 60) {
    return 0;
  }

  int64_t epoch =
      daysFromCivil(year, static_cast<unsigned>(month),
                    static_cast<unsigned>(day)) * 86400LL +
      static_cast<int64_t>(hour) * 3600LL +
      static_cast<int64_t>(minute) * 60LL +
      second;

  const char* timezone = nullptr;
  for (const char* cursor = text + 19; *cursor != '\0'; ++cursor) {
    if (*cursor == 'Z' || *cursor == '+' || *cursor == '-') {
      timezone = cursor;
      break;
    }
  }

  if (!timezone) {
    struct tm local {};
    local.tm_year = year - 1900;
    local.tm_mon = month - 1;
    local.tm_mday = day;
    local.tm_hour = hour;
    local.tm_min = minute;
    local.tm_sec = second;
    local.tm_isdst = -1;
    return mktime(&local);
  }

  if (*timezone == 'Z') {
    return static_cast<time_t>(epoch);
  }

  if (strlen(timezone) < 6 || timezone[3] != ':') return 0;

  const int offsetHours = parseDigits(timezone, 1, 2);
  const int offsetMinutes = parseDigits(timezone, 4, 2);
  if (offsetHours < 0 || offsetHours > 23 ||
      offsetMinutes < 0 || offsetMinutes > 59) {
    return 0;
  }

  const int offsetSeconds =
      offsetHours * 3600 + offsetMinutes * 60;

  // +02:00 means local clock time is two hours ahead of UTC.
  epoch += (*timezone == '+') ? -offsetSeconds : offsetSeconds;
  return static_cast<time_t>(epoch);
}

int roundDelayMinutes(int delaySeconds) {
  const long magnitude = delaySeconds < 0
      ? -static_cast<long>(delaySeconds)
      : static_cast<long>(delaySeconds);
  const int rounded = static_cast<int>((magnitude + 30L) / 60L);
  return delaySeconds < 0 ? -rounded : rounded;
}

void addSorted(
    Departure output[MAX_DISPLAY_DEPARTURES],
    int& count,
    const Departure& value) {
  int insertionIndex = count;

  for (int i = 0; i < count; ++i) {
    if (value.when < output[i].when) {
      insertionIndex = i;
      break;
    }
  }

  if (insertionIndex >= MAX_DISPLAY_DEPARTURES) {
    return;
  }

  const int lastIndex =
      count < MAX_DISPLAY_DEPARTURES
          ? count
          : MAX_DISPLAY_DEPARTURES - 1;

  for (int i = lastIndex; i > insertionIndex; --i) {
    output[i] = output[i - 1];
  }

  output[insertionIndex] = value;
  if (count < MAX_DISPLAY_DEPARTURES) {
    ++count;
  }
}

int fetchDepartures(Departure output[MAX_DISPLAY_DEPARTURES]) {
  if (stopId.isEmpty()) {
    lastError = "Stop ID missing";
    return -1;
  }

  const String url = String(API_BASE_URL) + "/stops/" + stopId +
      "/departures?results=6&duration=60&" +
      TRAM_PRODUCTS + DEPARTURE_API_PARAMS +
      "&language=" + LANGUAGE + COMMON_API_PARAMS;

  JsonDocument result;
  if (!getJson(url, result, departureJsonFilter(),
               "BVG: loading departures")) {
    return -1;
  }

  const time_t now = time(nullptr);
  int count = 0;

  for (JsonObject item : result["departures"].as<JsonArray>()) {
    if ((item["cancelled"] | false) ||
        String(item["line"]["product"] | "") != "tram") {
      continue;
    }

    const char* timestamp = item["when"];
    if (!timestamp) timestamp = item["plannedWhen"];

    Departure departure;
    departure.line = String(item["line"]["name"] | "?");
    departure.direction = String(item["direction"] | TXT_UNKNOWN_DIRECTION);
    departure.when = parseApiTime(timestamp);

    const int delaySeconds = item["delay"] | 0;
    departure.delayMinutes = roundDelayMinutes(delaySeconds);

    if (departure.when >= now - 60) {
      addSorted(output, count, departure);
    }
  }

  return count;
}

void printOverview(
    const Departure departures[MAX_DISPLAY_DEPARTURES], int count) {
  const time_t now = time(nullptr);

  Serial.println();
  Serial.println("----------------------------------------");
  Serial.printf("Aktuelle Zeit: %s\n", hhmm(now).c_str());

  if (count < 0) {
    Serial.println("Abfahrten konnten nicht geladen werden.");
    return;
  }

  if (count == 0) {
    Serial.println("Keine Tram-Abfahrten in den naechsten 60 Minuten.");
    return;
  }

  for (int i = 0; i < count; ++i) {
    long seconds = departures[i].when - now;
    if (seconds < 0) seconds = 0;
    const int minutes = static_cast<int>((seconds + 59) / 60);

    Serial.printf("%s %+d Tram %s -> %s (%d %s)\n",
                  hhmm(departures[i].when).c_str(),
                  departures[i].delayMinutes,
                  departures[i].line.c_str(),
                  departures[i].direction.c_str(),
                  minutes,
                  minutes == 1 ? "Minute" : "Minuten");
  }
}

bool refreshDepartures(bool initialRefresh = false) {
  if (WiFi.status() != WL_CONNECTED && !connectWifi()) {
    if (haveDepartureData) drawDepartureScreen(false);
    return false;
  }

  Departure fresh[MAX_DISPLAY_DEPARTURES];
  const int count = fetchDepartures(fresh);

  if (count < 0) {
    Serial.println("Abfahrten konnten nicht geladen werden.");
    if (haveDepartureData) {
      drawDepartureScreen(false);  // Keep old data and show error in footer.
    } else {
      drawCenteredMessage(TXT_BVG_ERROR, lastError, false);
    }
    return false;
  }

  cachedDepartureCount = count;
  for (int i = 0; i < count && i < MAX_DISPLAY_DEPARTURES; ++i) {
    cachedDepartures[i] = fresh[i];
  }

  haveDepartureData = true;
  lastSuccessfulUpdate = time(nullptr);
  lastError = "";

  printOverview(cachedDepartures, cachedDepartureCount);
  drawDepartureScreen(initialRefresh);
  return true;
}

// -----------------------------------------------------------------------------
// Arduino lifecycle
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("========================================");
  Serial.println("BVG tram display boot");
  Serial.printf("Reset: %s\n", resetReasonText().c_str());

  initializeDisplay();

  if (!readPassword()) {
    drawCenteredMessage(TXT_CONFIG_ERROR, lastError, false);
    Serial.println("Stopped: correct the password file and reset.");
    while (true) delay(1000);
  }

  if (!connectWifi()) {
    drawCenteredMessage(TXT_WIFI_ERROR, lastError, false);
    Serial.println("Restarting in 30 seconds...");
    delay(30000);
    ESP.restart();
  }

  if (!synchronizeClock()) {
    drawCenteredMessage(TXT_TIME_ERROR, lastError, false);
    Serial.println("Restarting in 30 seconds...");
    delay(30000);
    ESP.restart();
  }

  int stopAttempts = 0;
  while (stopId.isEmpty() && stopAttempts < 3) {
    ++stopAttempts;
    if (findStopId()) break;
    appendBootLog("Stop retry " + String(stopAttempts) + "/3");
    delay(5000);
  }

  if (stopId.isEmpty()) {
    drawCenteredMessage(TXT_BVG_ERROR, lastError, false);
    Serial.println("Restarting in 30 seconds...");
    delay(30000);
    ESP.restart();
  }

  appendBootLog("Loading first overview");
  const bool initialSuccess = refreshDepartures(true);
  currentRefreshInterval = initialSuccess ? REFRESH_MS : ERROR_RETRY_MS;
  lastRefresh = millis();
}

void loop() {
  if (millis() - lastRefresh >= currentRefreshInterval) {
    const bool success = refreshDepartures(false);
    currentRefreshInterval = success ? REFRESH_MS : ERROR_RETRY_MS;
    lastRefresh = millis();
  }

  delay(100);
}
