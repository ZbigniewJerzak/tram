#include <Adafruit_GFX.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>

#include <climits>
#include <cstring>

#include "BitmapFont.h"
#include "ElecrowEpd579.h"
#include "SpleenFontData.h"

namespace
{
constexpr uint16_t COLOR_WHITE = 0;
constexpr uint16_t COLOR_BLACK = 1;
constexpr uint16_t CONTROLLER_SEAM_X = 396;
constexpr uint16_t SEAM_ADDRESS_OFFSET = 8;
constexpr size_t DRIVER_ROW_BYTES = ElecrowEpd579::DRIVER_WIDTH / 8;
constexpr char WIFI_SSID[] = "birnensaft";
constexpr char PASSWORD_FILE[] = "/wifi-password.txt";
constexpr char SENSOR_URL[] =
    "http://192.168.187.60/v4/devices/?devtype=Sensor";
constexpr unsigned long WIFI_TIMEOUT_MS = 30000UL;
constexpr unsigned long HTTP_TIMEOUT_MS = 15000UL;

const IPAddress STATIC_IP(192, 168, 187, 190);
const IPAddress GATEWAY(192, 168, 187, 1);
const IPAddress SUBNET(255, 255, 255, 0);
const IPAddress DNS_SERVER(192, 168, 187, 1);

ElecrowEpd579 display;
uint8_t* displayBuffer = nullptr;
String wifiPassword;
String lastError;

struct WeatherData
{
    bool found = false;
    bool statusValid = false;
    String name;
    bool hasTemperature = false;
    float temperature = 0.0F;
    bool hasWind = false;
    float wind = 0.0F;
    bool hasRain = false;
    bool rain = false;
    bool hasBrightness = false;
    long brightness = 0;
};

struct FireData
{
    bool found = false;
    bool statusValid = false;
    uint16_t deviceCount = 0;
    String name;
    bool alarmKnown = false;
    bool alarm = false;
    String alarmReading;
    int batteryStatus = -1;
    bool batteryLow = false;
};

WeatherData weatherData;
FireData fireData;

class TextCanvas : public Adafruit_GFX
{
public:
    explicit TextCanvas(uint8_t* const frameBuffer)
        : Adafruit_GFX(ElecrowEpd579::VISIBLE_WIDTH, ElecrowEpd579::HEIGHT),
          frameBuffer_(frameBuffer)
    {
    }

    void clear()
    {
        memset(frameBuffer_, 0xFF, ElecrowEpd579::FRAMEBUFFER_SIZE);
    }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override
    {
        if (x < 0 || y < 0 || x >= width() || y >= height())
        {
            return;
        }

        const uint16_t visibleX = static_cast<uint16_t>(x);
        const uint16_t visibleY = static_cast<uint16_t>(y);
        const uint16_t shiftedX = visibleX < CONTROLLER_SEAM_X
            ? visibleX
            : visibleX + SEAM_ADDRESS_OFFSET;
        const uint16_t driverX =
            ElecrowEpd579::DRIVER_WIDTH - shiftedX - 1;
        const uint16_t driverY = ElecrowEpd579::HEIGHT - visibleY - 1;
        const size_t address =
            static_cast<size_t>(driverY) * DRIVER_ROW_BYTES + driverX / 8;
        const uint8_t mask = static_cast<uint8_t>(0x80U >> (driverX % 8));

        if (color == COLOR_BLACK)
        {
            frameBuffer_[address] &= static_cast<uint8_t>(~mask);
        }
        else
        {
            frameBuffer_[address] |= mask;
        }
    }

private:
    uint8_t* frameBuffer_;
};

struct HorizontalBounds
{
    int16_t minimumX;
    int16_t maximumX;
};

bool readGlyph(
    const BitmapFont& font,
    const uint32_t codepoint,
    BitmapGlyph& glyph
)
{
    for (uint16_t index = 0; index < font.glyphCount; ++index)
    {
        memcpy_P(&glyph, &font.glyphs[index], sizeof(glyph));
        if (glyph.codepoint == codepoint)
        {
            return true;
        }
    }
    return false;
}

uint32_t readUtf8Codepoint(const char*& text)
{
    const uint8_t first = static_cast<uint8_t>(*text++);
    if (first < 0x80U)
    {
        return first;
    }
    if ((first & 0xE0U) == 0xC0U)
    {
        const uint8_t second = static_cast<uint8_t>(*text++);
        if ((second & 0xC0U) == 0x80U)
        {
            return ((first & 0x1FU) << 6U) | (second & 0x3FU);
        }
    }
    else if ((first & 0xF0U) == 0xE0U)
    {
        const uint8_t second = static_cast<uint8_t>(*text++);
        const uint8_t third = static_cast<uint8_t>(*text++);
        if ((second & 0xC0U) == 0x80U && (third & 0xC0U) == 0x80U)
        {
            return ((first & 0x0FU) << 12U) |
                ((second & 0x3FU) << 6U) |
                (third & 0x3FU);
        }
    }
    return '?';
}

void drawGlyph(
    TextCanvas& canvas,
    const BitmapFont& font,
    const BitmapGlyph& glyph,
    const int16_t cursorX,
    const int16_t baselineY
)
{
    uint32_t bitIndex = 0;
    for (uint8_t row = 0; row < glyph.height; ++row)
    {
        for (uint8_t column = 0; column < glyph.width; ++column)
        {
            const uint8_t value = pgm_read_byte(
                font.bitmap + glyph.bitmapOffset + bitIndex / 8U
            );
            const uint8_t mask = static_cast<uint8_t>(0x80U >> (bitIndex % 8U));
            if ((value & mask) != 0U)
            {
                canvas.drawPixel(
                    cursorX + glyph.xOffset + column,
                    baselineY + glyph.yOffset + row,
                    COLOR_BLACK
                );
            }
            ++bitIndex;
        }
    }
}

int16_t drawCodepoint(
    TextCanvas& canvas,
    const BitmapFont& font,
    const uint32_t codepoint,
    const int16_t cursorX,
    const int16_t baselineY
)
{
    BitmapGlyph glyph = {};
    if (!readGlyph(font, codepoint, glyph))
    {
        readGlyph(font, '?', glyph);
    }
    drawGlyph(canvas, font, glyph, cursorX, baselineY);
    return cursorX + glyph.xAdvance;
}

HorizontalBounds measureText(const BitmapFont& font, const char* text)
{
    HorizontalBounds bounds = {INT16_MAX, INT16_MIN};
    int16_t cursorX = 0;
    while (*text != '\0')
    {
        const uint32_t codepoint = readUtf8Codepoint(text);
        BitmapGlyph glyph = {};
        if (!readGlyph(font, codepoint, glyph))
        {
            readGlyph(font, '?', glyph);
        }
        if (glyph.width > 0)
        {
            const int16_t glyphLeft = cursorX + glyph.xOffset;
            const int16_t glyphRight = glyphLeft + glyph.width;
            if (glyphLeft < bounds.minimumX)
            {
                bounds.minimumX = glyphLeft;
            }
            if (glyphRight > bounds.maximumX)
            {
                bounds.maximumX = glyphRight;
            }
        }
        cursorX += glyph.xAdvance;
    }
    if (bounds.minimumX == INT16_MAX)
    {
        bounds = {0, 0};
    }
    return bounds;
}

void drawText(
    TextCanvas& canvas,
    const BitmapFont& font,
    const char* text,
    int16_t cursorX,
    const int16_t baselineY
)
{
    while (*text != '\0')
    {
        cursorX = drawCodepoint(
            canvas,
            font,
            readUtf8Codepoint(text),
            cursorX,
            baselineY
        );
    }
}

void drawCenteredText(
    TextCanvas& canvas,
    const BitmapFont& font,
    const char* const text,
    const int16_t baselineY
)
{
    const HorizontalBounds bounds = measureText(font, text);
    const int16_t width = bounds.maximumX - bounds.minimumX;
    drawText(
        canvas,
        font,
        text,
        (canvas.width() - width) / 2 - bounds.minimumX,
        baselineY
    );
}

bool allocateDisplayBuffer()
{
    displayBuffer = static_cast<uint8_t*>(
        ps_malloc(ElecrowEpd579::FRAMEBUFFER_SIZE)
    );
    if (displayBuffer == nullptr)
    {
        return false;
    }
    memset(displayBuffer, 0xFF, ElecrowEpd579::FRAMEBUFFER_SIZE);
    return true;
}

void drawCardHeading(
    TextCanvas& canvas,
    const char* const heading,
    const int16_t centerX
)
{
    const HorizontalBounds bounds = measureText(Spleen12x24, heading);
    const int16_t width = bounds.maximumX - bounds.minimumX;
    drawText(
        canvas,
        Spleen12x24,
        heading,
        centerX - width / 2 - bounds.minimumX,
        75
    );
}

void drawCenteredInCard(
    TextCanvas& canvas,
    const BitmapFont& font,
    const String& text,
    const int16_t centerX,
    const int16_t baselineY
)
{
    const HorizontalBounds bounds = measureText(font, text.c_str());
    const int16_t width = bounds.maximumX - bounds.minimumX;
    drawText(
        canvas,
        font,
        text.c_str(),
        centerX - width / 2 - bounds.minimumX,
        baselineY
    );
}

String yesNo(const bool value)
{
    return value ? "JA" : "NEIN";
}

void drawSensorScreen(TextCanvas& canvas)
{
    constexpr int16_t LEFT_CENTER = 201;
    constexpr int16_t RIGHT_CENTER = 591;

    canvas.clear();
    canvas.drawRect(0, 0, canvas.width(), canvas.height(), COLOR_BLACK);
    canvas.drawRect(4, 4, canvas.width() - 8, canvas.height() - 8, COLOR_BLACK);

    drawCenteredText(canvas, Spleen12x24, "HOMEPILOT SENSOREN", 30);
    canvas.drawFastHLine(20, 42, canvas.width() - 40, COLOR_BLACK);
    canvas.drawRect(16, 51, 370, 169, COLOR_BLACK);
    canvas.drawRect(406, 51, 370, 169, COLOR_BLACK);

    if (!lastError.isEmpty())
    {
        drawCenteredText(canvas, Spleen16x32, "NETZWERKFEHLER", 115);
        drawCenteredText(canvas, Spleen8x16, lastError.c_str(), 151);
    }
    else
    {
        drawCardHeading(canvas, "AUSSENWETTER", LEFT_CENTER);
        if (weatherData.found)
        {
            String temperature = weatherData.hasTemperature
                ? String(weatherData.temperature, 1) + "°C"
                : "--.-°C";
            drawCenteredInCard(
                canvas,
                Spleen16x32,
                temperature,
                LEFT_CENTER,
                116
            );

            const String wind = weatherData.hasWind
                ? "Wind: " + String(weatherData.wind, 1) + " m/s"
                : "Wind: --";
            drawText(canvas, Spleen8x16, wind.c_str(), 36, 147);

            const String rain = weatherData.hasRain
                ? "Regen: " + yesNo(weatherData.rain)
                : "Regen: --";
            drawText(canvas, Spleen8x16, rain.c_str(), 36, 169);

            const String brightness = weatherData.hasBrightness
                ? "Helligkeit: " + String(weatherData.brightness)
                : "Helligkeit: --";
            drawText(canvas, Spleen8x16, brightness.c_str(), 36, 191);
            drawText(
                canvas,
                Spleen8x16,
                weatherData.statusValid ? "Sensorstatus: OK" : "Sensorstatus: FEHLER",
                36,
                211
            );
        }
        else
        {
            drawCenteredInCard(
                canvas,
                Spleen12x24,
                "NICHT GEFUNDEN",
                LEFT_CENTER,
                139
            );
        }

        drawCardHeading(canvas, "RAUCHMELDER", RIGHT_CENTER);
        if (fireData.found)
        {
            const String alarmText = fireData.alarmKnown
                ? (fireData.alarm ? "ALARM!" : "OK")
                : "STATUS ?";
            drawCenteredInCard(
                canvas,
                Spleen16x32,
                alarmText,
                RIGHT_CENTER,
                116
            );

            String count = String(fireData.deviceCount) + " Melder erkannt";
            drawText(canvas, Spleen8x16, count.c_str(), 426, 147);

            const String battery = fireData.batteryStatus >= 0
                ? "Batterie min: " + String(fireData.batteryStatus) + "%"
                : "Batterie: --";
            drawText(canvas, Spleen8x16, battery.c_str(), 426, 169);
            drawText(
                canvas,
                Spleen8x16,
                fireData.batteryLow ? "Batteriewarnung: JA" : "Batteriewarnung: NEIN",
                426,
                191
            );
            drawText(
                canvas,
                Spleen8x16,
                fireData.statusValid ? "Sensorstatus: OK" : "Sensorstatus: FEHLER",
                426,
                211
            );
        }
        else
        {
            drawCenteredInCard(
                canvas,
                Spleen12x24,
                "NICHT GEFUNDEN",
                RIGHT_CENTER,
                139
            );
        }
    }

    drawText(canvas, Spleen8x16, "ESP32: 192.168.187.190", 20, 249);
    drawText(canvas, Spleen8x16, "HUB: 192.168.187.60", 580, 249);
}

bool readPassword()
{
    Serial.println("LittleFS: einbinden");
    if (!LittleFS.begin(false))
    {
        lastError = "LittleFS konnte nicht eingebunden werden";
        return false;
    }

    File file = LittleFS.open(PASSWORD_FILE, "r");
    if (!file)
    {
        lastError = "wifi-password.txt fehlt";
        return false;
    }

    wifiPassword = file.readString();
    file.close();
    wifiPassword.trim();
    if (wifiPassword.isEmpty())
    {
        lastError = "wifi-password.txt ist leer";
        return false;
    }

    Serial.println("LittleFS: WLAN-Passwort geladen");
    return true;
}

bool connectWifi()
{
    Serial.printf("WLAN: verbinde mit %s\n", WIFI_SSID);
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    delay(250);

    if (!WiFi.config(STATIC_IP, GATEWAY, SUBNET, DNS_SERVER))
    {
        lastError = "Statische IP konnte nicht gesetzt werden";
        return false;
    }

    WiFi.begin(WIFI_SSID, wifiPassword.c_str());
    const unsigned long started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < WIFI_TIMEOUT_MS)
    {
        delay(250);
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        lastError = "WLAN-Verbindung fehlgeschlagen";
        return false;
    }

    if (WiFi.localIP() != STATIC_IP)
    {
        lastError = "Unerwartete lokale IP: " + WiFi.localIP().toString();
        return false;
    }

    Serial.printf(
        "WLAN: verbunden, IP %s, RSSI %d dBm\n",
        WiFi.localIP().toString().c_str(),
        WiFi.RSSI()
    );
    return true;
}

bool isAlarmKey(String key)
{
    key.toLowerCase();
    return key.indexOf("smoke") >= 0 ||
        key.indexOf("rauch") >= 0 ||
        key.indexOf("fire") >= 0 ||
        key.indexOf("brand") >= 0 ||
        key.indexOf("alarm") >= 0;
}

bool nameLooksLikeFireSensor(String name)
{
    name.toLowerCase();
    return name.indexOf("rauch") >= 0 ||
        name.indexOf("smoke") >= 0 ||
        name.indexOf("brand") >= 0 ||
        name.indexOf("fire") >= 0;
}

bool parseAlarmValue(JsonVariantConst value, bool& alarm)
{
    if (value.is<bool>())
    {
        alarm = value.as<bool>();
        return true;
    }
    if (value.is<long>() || value.is<unsigned long>())
    {
        alarm = value.as<long>() != 0;
        return true;
    }
    if (!value.is<const char*>())
    {
        return false;
    }

    String text = value.as<const char*>();
    text.trim();
    text.toLowerCase();
    if (text == "false" || text == "no" || text == "off" ||
        text == "idle" || text == "ok" || text == "normal" ||
        text == "none" || text == "0")
    {
        alarm = false;
        return true;
    }
    if (text == "true" || text == "yes" || text == "on" ||
        text == "alarm" || text == "smoke" || text == "fire" ||
        text == "detected" || text == "active" || text == "1")
    {
        alarm = true;
        return true;
    }
    return false;
}

void parseSensors(const JsonDocument& document)
{
    weatherData = WeatherData{};
    fireData = FireData{};
    int bestWeatherScore = 0;

    for (JsonObjectConst meter : document["meters"].as<JsonArrayConst>())
    {
        const JsonObjectConst readings = meter["readings"].as<JsonObjectConst>();
        const String name = meter["name"] | "";
        const String deviceNumber = meter["deviceNumber"] | "";

        WeatherData candidate;
        candidate.name = name;
        candidate.statusValid = meter["statusValid"] | false;
        candidate.hasTemperature = readings["temperature_primary"].is<float>() ||
            readings["temperature_primary"].is<long>();
        candidate.temperature = readings["temperature_primary"] | 0.0F;
        candidate.hasWind = readings["wind_speed"].is<float>() ||
            readings["wind_speed"].is<long>();
        candidate.wind = readings["wind_speed"] | 0.0F;
        candidate.hasRain = readings["rain_detected"].is<bool>();
        candidate.rain = readings["rain_detected"] | false;
        candidate.hasBrightness = readings["sun_brightness"].is<long>();
        candidate.brightness = readings["sun_brightness"] | 0L;

        const int weatherScore =
            static_cast<int>(candidate.hasTemperature) +
            static_cast<int>(candidate.hasWind) +
            static_cast<int>(candidate.hasRain) +
            static_cast<int>(candidate.hasBrightness);
        if (weatherScore > bestWeatherScore)
        {
            candidate.found = true;
            weatherData = candidate;
            bestWeatherScore = weatherScore;
        }

        bool fireCandidate = nameLooksLikeFireSensor(name) ||
            deviceNumber == "32001664";
        bool meterAlarmKnown = false;
        bool meterAlarm = false;
        for (JsonPairConst reading : readings)
        {
            if (!isAlarmKey(reading.key().c_str()))
            {
                continue;
            }
            fireCandidate = true;
            bool parsedAlarm = false;
            if (parseAlarmValue(reading.value(), parsedAlarm))
            {
                meterAlarmKnown = true;
                meterAlarm = meterAlarm || parsedAlarm;
            }
        }

        if (fireCandidate)
        {
            if (!fireData.found)
            {
                fireData.found = true;
                fireData.statusValid = meter["statusValid"] | false;
                fireData.name = name;
                fireData.batteryStatus = meter["batteryStatus"] | -1;
            }
            else
            {
                fireData.statusValid = fireData.statusValid &&
                    static_cast<bool>(meter["statusValid"] | false);
                const int battery = meter["batteryStatus"] | -1;
                if (battery >= 0 &&
                    (fireData.batteryStatus < 0 || battery < fireData.batteryStatus))
                {
                    fireData.batteryStatus = battery;
                }
            }
            ++fireData.deviceCount;
            fireData.batteryLow = fireData.batteryLow ||
                static_cast<bool>(meter["batteryLow"] | false);
            fireData.alarmKnown = fireData.alarmKnown || meterAlarmKnown;
            fireData.alarm = fireData.alarm || meterAlarm;
        }
    }

    Serial.printf(
        "Sensoren: Wetter=%s, Rauchmelder=%u, Alarm=%s\n",
        weatherData.found ? "gefunden" : "nicht gefunden",
        fireData.deviceCount,
        fireData.alarmKnown ? (fireData.alarm ? "JA" : "NEIN") : "unbekannt"
    );
}

bool fetchSensors()
{
    Serial.printf("HTTP GET %s\n", SENSOR_URL);
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.useHTTP10(true);
    if (!http.begin(SENSOR_URL))
    {
        lastError = "HTTP-Initialisierung fehlgeschlagen";
        return false;
    }
    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", "CrowPanel-Spleen-Sensor/1.0");

    const int status = http.GET();
    if (status != HTTP_CODE_OK)
    {
        lastError = status < 0
            ? "Hub nicht erreichbar"
            : "Hub antwortet mit HTTP " + String(status);
        http.end();
        return false;
    }

    JsonDocument filter;
    filter["meters"][0]["name"] = true;
    filter["meters"][0]["deviceNumber"] = true;
    filter["meters"][0]["statusValid"] = true;
    filter["meters"][0]["batteryStatus"] = true;
    filter["meters"][0]["batteryLow"] = true;
    filter["meters"][0]["readings"] = true;

    JsonDocument document;
    const DeserializationError error = deserializeJson(
        document,
        http.getStream(),
        DeserializationOption::Filter(filter)
    );
    http.end();
    if (error)
    {
        lastError = "JSON-Fehler: " + String(error.c_str());
        return false;
    }
    if (!document["meters"].is<JsonArray>())
    {
        lastError = "JSON enthält kein meters-Array";
        return false;
    }

    parseSensors(document);
    lastError = "";
    return true;
}

bool refreshDisplay()
{
    Serial.println("Display: Fast-Mode initialisieren");
    if (!display.initializeFastMode())
    {
        Serial.println("FEHLER: BUSY-Timeout bei der Initialisierung.");
        return false;
    }

    Serial.println("Display: Controller-Speicher loeschen");
    display.clearDisplayMemory();
    Serial.println("Display: vollstaendigen Loeschzyklus starten");
    if (!display.updateFull())
    {
        Serial.println("FEHLER: BUSY-Timeout beim Loeschzyklus.");
        return false;
    }

    Serial.println("Display: fuer Sensorbild neu initialisieren");
    if (!display.initializeFastMode())
    {
        Serial.println("FEHLER: BUSY-Timeout vor dem Sensorbild.");
        return false;
    }

    Serial.println("Display: Sensorbild uebertragen");
    if (!display.writeFramebuffer(displayBuffer))
    {
        Serial.println("FEHLER: Framebuffer konnte nicht uebertragen werden.");
        return false;
    }

    Serial.println("Display: schnellen Bildaufbau starten");
    if (!display.updateFast())
    {
        Serial.println("FEHLER: BUSY-Timeout beim Bildaufbau.");
        return false;
    }

    display.deepSleep();
    return true;
}
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("CrowPanel 5.79-inch Spleen WiFi sensor test");
    Serial.println("Static IP: 192.168.187.190/24");

    if (!psramFound())
    {
        Serial.println("FEHLER: OPI-PSRAM wurde nicht erkannt.");
        return;
    }
    if (!allocateDisplayBuffer())
    {
        Serial.println("FEHLER: Displaybuffer konnte nicht angelegt werden.");
        return;
    }

    display.begin();

    const bool configured = readPassword();
    const bool connected = configured && connectWifi();
    if (connected)
    {
        fetchSensors();
    }

    TextCanvas canvas(displayBuffer);
    drawSensorScreen(canvas);

    if (refreshDisplay())
    {
        Serial.println("Sensortest abgeschlossen; Display ist im Deep Sleep.");
    }

    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
}

void loop()
{
    delay(1000);
}
