#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>

#include <cmath>
#include <cstring>
#include <time.h>

#include "ElecrowEpd579.h"
#include "LocalWeatherSensor.h"
#include "OpenWeatherClient.h"
#include "SpleenCanvas.h"
#include "SpleenFontData.h"
#include "WeatherData.h"
#include "WeatherSymbols.h"

namespace
{
constexpr uint16_t COLOR_BLACK = 1;
constexpr char WIFI_SSID[] = "birnensaft";
constexpr char WIFI_PASSWORD_FILE[] = "/wifi-password.txt";
constexpr char OPENWEATHER_KEY_FILE[] = "/openweather.txt";
constexpr char LOCAL_SENSOR_URL[] =
    "http://192.168.187.60/v4/devices/?devtype=Sensor";
constexpr char BERLIN_TIMEZONE[] = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr char LOCAL_NTP_SERVER[] = "192.168.187.1";
constexpr char FALLBACK_NTP_SERVER_1[] = "pool.ntp.org";
constexpr char FALLBACK_NTP_SERVER_2[] = "time.cloudflare.com";
constexpr float LATITUDE = 52.482781F;
constexpr float LONGITUDE = 13.603978F;
constexpr unsigned long WIFI_TIMEOUT_MS = 30000UL;
constexpr unsigned long TIME_TIMEOUT_MS = 25000UL;
constexpr unsigned long LOCAL_INTERVAL_MS = 60UL * 1000UL;
constexpr unsigned long OPENWEATHER_INTERVAL_MS = 10UL * 60UL * 1000UL;
constexpr int16_t WEATHER_PANEL_LEFT = 528;
constexpr int16_t WEATHER_PANEL_WIDTH = 264;
constexpr int16_t WEATHER_CONTENT_LEFT = 536;
constexpr int16_t WEATHER_CONTENT_RIGHT = 784;
constexpr int16_t FIRST_SYMBOL_X = 536;
constexpr int16_t FIRST_VALUE_X = 568;
constexpr int16_t SECOND_SYMBOL_X = 660;
constexpr int16_t SECOND_VALUE_X = 692;
constexpr int16_t FORECAST_VALUE_WIDTH = 80;
constexpr int16_t CURRENT_ROW_BOTTOM = 64;
constexpr int16_t TODAY_ROW_TOP = 65;
constexpr int16_t TODAY_ROW_BOTTOM = 203;
constexpr int16_t TOMORROW_ROW_TOP = 204;

static_assert(
    WEATHER_PANEL_LEFT == ElecrowEpd579::VISIBLE_WIDTH * 2 / 3
);
static_assert(
    WEATHER_PANEL_LEFT + WEATHER_PANEL_WIDTH ==
        ElecrowEpd579::VISIBLE_WIDTH
);

const IPAddress STATIC_IP(192, 168, 187, 195);
const IPAddress GATEWAY(192, 168, 187, 1);
const IPAddress SUBNET(255, 255, 255, 0);
const IPAddress PRIMARY_DNS_SERVER(192, 168, 187, 10);
const IPAddress FALLBACK_DNS_SERVER(192, 168, 187, 1);

const char* const WEEKDAYS[] = {
    "Sonntag", "Montag", "Dienstag", "Mittwoch",
    "Donnerstag", "Freitag", "Samstag"
};

ElecrowEpd579 display;
LocalWeatherSensor localSensor(LOCAL_SENSOR_URL);
OpenWeatherClient openWeather(LATITUDE, LONGITUDE);
uint8_t* displayBuffer = nullptr;
String wifiPassword;
String openWeatherKey;
String fatalError;
String localError;
String forecastError;
LocalWeatherReading localWeather;
WeatherForecast forecast;
bool haveLocalWeather = false;
bool systemReady = false;
bool timeReady = false;
unsigned long lastLocalAttemptMs = 0;
unsigned long lastOpenWeatherAttemptMs = 0;
unsigned long lastTimeAttemptMs = 0;

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

bool readSecret(const char* const path, String& value, String& error)
{
    File file = LittleFS.open(path, "r");
    if (!file)
    {
        error = String(path) + " fehlt";
        return false;
    }
    value = file.readString();
    file.close();
    value.trim();
    if (value.isEmpty())
    {
        error = String(path) + " ist leer";
        return false;
    }
    return true;
}

bool loadConfiguration()
{
    Serial.println("LittleFS: einbinden und Zugangsdaten lesen");
    if (!LittleFS.begin(false))
    {
        fatalError = "LittleFS konnte nicht eingebunden werden";
        return false;
    }
    if (!readSecret(WIFI_PASSWORD_FILE, wifiPassword, fatalError))
    {
        return false;
    }
    if (!readSecret(OPENWEATHER_KEY_FILE, openWeatherKey, fatalError))
    {
        return false;
    }
    Serial.println("LittleFS: beide Zugangsdaten geladen");
    return true;
}

bool connectWifi()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        return true;
    }

    Serial.printf("WLAN: verbinde mit %s\n", WIFI_SSID);
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    const bool sleepDisabled = WiFi.setSleep(false);
    Serial.printf(
        "WLAN: Modem-Sleep deaktivieren: %s\n",
        sleepDisabled ? "OK" : "FEHLER"
    );
    WiFi.setAutoReconnect(true);
    WiFi.disconnect(false, false);
    delay(250);
    if (!WiFi.config(
            STATIC_IP,
            GATEWAY,
            SUBNET,
            PRIMARY_DNS_SERVER,
            FALLBACK_DNS_SERVER
        ))
    {
        fatalError = "Statische IP konnte nicht gesetzt werden";
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
        fatalError = "WLAN-Verbindung fehlgeschlagen";
        return false;
    }

    Serial.printf(
        "WLAN: IP %s, RSSI %d dBm, DNS1 %s, DNS2 %s\n",
        WiFi.localIP().toString().c_str(),
        WiFi.RSSI(),
        WiFi.dnsIP(0).toString().c_str(),
        WiFi.dnsIP(1).toString().c_str()
    );
    fatalError = "";
    return true;
}

bool synchronizeTime()
{
    Serial.printf(
        "Zeit: NTP-Synchronisierung; lokal=%s, fallback=%s, %s\n",
        LOCAL_NTP_SERVER,
        FALLBACK_NTP_SERVER_1,
        FALLBACK_NTP_SERVER_2
    );
    configTzTime(
        BERLIN_TIMEZONE,
        LOCAL_NTP_SERVER,
        FALLBACK_NTP_SERVER_1,
        FALLBACK_NTP_SERVER_2
    );

    const unsigned long started = millis();
    unsigned long lastDiagnosticMs = started;
    tm localTime = {};
    while (millis() - started < TIME_TIMEOUT_MS)
    {
        if (getLocalTime(&localTime, 1000))
        {
            char synchronizedTime[32] = {};
            strftime(
                synchronizedTime,
                sizeof(synchronizedTime),
                "%d.%m.%Y %H:%M:%S %Z",
                &localTime
            );
            Serial.printf("Zeit: synchronisiert: %s\n", synchronizedTime);
            return true;
        }

        const unsigned long nowMs = millis();
        if (nowMs - lastDiagnosticMs >= 5000UL)
        {
            Serial.printf(
                "Zeit DEBUG: warte seit %lu ms; WLAN status=%d, "
                "RSSI=%d dBm, epoch=%lld\n",
                nowMs - started,
                static_cast<int>(WiFi.status()),
                WiFi.RSSI(),
                static_cast<long long>(time(nullptr))
            );
            lastDiagnosticMs = nowMs;
        }
    }
    Serial.printf(
        "Zeit FEHLER: kein NTP-Ergebnis nach %lu ms; WLAN status=%d, "
        "IP=%s, RSSI=%d dBm\n",
        millis() - started,
        static_cast<int>(WiFi.status()),
        WiFi.localIP().toString().c_str(),
        WiFi.RSSI()
    );
    fatalError = "NTP-Zeitsynchronisierung fehlgeschlagen";
    return false;
}

String conditionText(const WeatherCondition condition)
{
    switch (condition)
    {
        case WeatherCondition::SUN:
            return "SONNE";
        case WeatherCondition::CLOUDS:
            return "WOLKEN";
        case WeatherCondition::RAIN:
            return "REGEN";
        default:
            return "UNBEKANNT";
    }
}

const char* conditionSymbol(const WeatherCondition condition)
{
    switch (condition)
    {
        case WeatherCondition::SUN:
            return WeatherSymbols::SUN;
        case WeatherCondition::CLOUDS:
            return WeatherSymbols::CLOUD;
        case WeatherCondition::RAIN:
            return WeatherSymbols::RAIN;
        default:
            return "?";
    }
}

String temperatureText(const float value)
{
    return String(value, 1) + "°C";
}

String forecastTemperatureText(
    SpleenCanvas& canvas,
    const bool valid,
    const float value
)
{
    if (!valid)
    {
        return "--.-°C";
    }
    const String precise = temperatureText(value);
    if (canvas.textWidth(Spleen12x24, precise) <= FORECAST_VALUE_WIDTH)
    {
        return precise;
    }
    return String(static_cast<long>(lroundf(value))) + "°C";
}

String clockText(const uint8_t hour, const uint8_t minute)
{
    char value[6] = {};
    snprintf(value, sizeof(value), "%02u:%02u", hour, minute);
    return value;
}

String forecastStatusText(const ForecastSummary& summary)
{
    String value = String(summary.sampleCount) + " WERTE";
    if (!forecastError.isEmpty())
    {
        value += " OW!";
    }
    return value;
}

void drawVerticalArrow(
    SpleenCanvas& canvas,
    const int16_t left,
    const int16_t top,
    const bool pointsUp
)
{
    constexpr int16_t ARROW_WIDTH = 12;
    constexpr int16_t ARROW_HEIGHT = 16;
    const int16_t centerX = left + ARROW_WIDTH / 2;
    if (pointsUp)
    {
        canvas.drawFastVLine(
            centerX,
            top,
            ARROW_HEIGHT,
            COLOR_BLACK
        );
        canvas.drawLine(centerX, top, left, top + 6, COLOR_BLACK);
        canvas.drawLine(
            centerX,
            top,
            left + ARROW_WIDTH,
            top + 6,
            COLOR_BLACK
        );
    }
    else
    {
        const int16_t bottom = top + ARROW_HEIGHT - 1;
        canvas.drawFastVLine(
            centerX,
            top,
            ARROW_HEIGHT,
            COLOR_BLACK
        );
        canvas.drawLine(centerX, bottom, left, bottom - 6, COLOR_BLACK);
        canvas.drawLine(
            centerX,
            bottom,
            left + ARROW_WIDTH,
            bottom - 6,
            COLOR_BLACK
        );
    }
}

void drawSummary(
    SpleenCanvas& canvas,
    const ForecastSummary& summary,
    const int16_t rowTop,
    const char* const heading,
    const SolarTimes* const solarTimes,
    const bool detailed
)
{
    const BitmapFont& headingFont = Spleen8x16;
    const int16_t headingBaseline = rowTop + 14;
    canvas.drawText(
        headingFont,
        heading,
        WEATHER_CONTENT_LEFT,
        headingBaseline
    );
    canvas.drawRightAlignedText(
        headingFont,
        forecastStatusText(summary),
        WEATHER_CONTENT_RIGHT,
        headingBaseline
    );
    if (solarTimes != nullptr && solarTimes->valid)
    {
        constexpr int16_t TRIANGLE_INSET = 3;
        constexpr int16_t TRIANGLE_WIDTH = 18;
        const int16_t triangleTop = rowTop + 30;
        const int16_t triangleBottom = rowTop + 45;
        canvas.fillTriangle(
            FIRST_SYMBOL_X + TRIANGLE_INSET + TRIANGLE_WIDTH / 2,
            triangleTop,
            FIRST_SYMBOL_X + TRIANGLE_INSET,
            triangleBottom,
            FIRST_SYMBOL_X + TRIANGLE_INSET + TRIANGLE_WIDTH,
            triangleBottom,
            COLOR_BLACK
        );
        canvas.drawTriangle(
            SECOND_SYMBOL_X + TRIANGLE_INSET,
            triangleTop,
            SECOND_SYMBOL_X + TRIANGLE_INSET + TRIANGLE_WIDTH,
            triangleTop,
            SECOND_SYMBOL_X + TRIANGLE_INSET + TRIANGLE_WIDTH / 2,
            triangleBottom,
            COLOR_BLACK
        );
        canvas.drawText(
            Spleen12x24,
            clockText(solarTimes->sunriseHour, solarTimes->sunriseMinute),
            FIRST_VALUE_X,
            rowTop + 47
        );
        canvas.drawText(
            Spleen12x24,
            clockText(solarTimes->sunsetHour, solarTimes->sunsetMinute),
            SECOND_VALUE_X,
            rowTop + 47
        );
    }
    if (!summary.valid)
    {
        canvas.drawText(
            Spleen12x24,
            "KEINE DATEN",
            WEATHER_CONTENT_LEFT,
            rowTop + (detailed ? 80 : 42)
        );
        return;
    }

    const BitmapFont& averageFont = Spleen12x24;
    const int16_t averageBaseline = rowTop + (detailed ? 73 : 40);
    canvas.drawText(
        averageFont,
        WeatherSymbols::DAY,
        FIRST_SYMBOL_X,
        averageBaseline
    );
    canvas.drawText(
        averageFont,
        forecastTemperatureText(
            canvas,
            summary.daytimeTemperatureValid,
            summary.daytimeWeightedTemperature
        ),
        FIRST_VALUE_X,
        averageBaseline
    );
    canvas.drawText(
        averageFont,
        WeatherSymbols::NIGHT,
        SECOND_SYMBOL_X,
        averageBaseline
    );
    canvas.drawText(
        averageFont,
        forecastTemperatureText(
            canvas,
            summary.nighttimeTemperatureValid,
            summary.nighttimeWeightedTemperature
        ),
        SECOND_VALUE_X,
        averageBaseline
    );
    if (detailed)
    {
        const int16_t temperatureBaseline = rowTop + 99;
        const int16_t arrowTop = temperatureBaseline - 17;
        drawVerticalArrow(canvas, FIRST_SYMBOL_X + 6, arrowTop, false);
        drawVerticalArrow(canvas, SECOND_SYMBOL_X + 6, arrowTop, true);
        canvas.drawText(
            Spleen12x24,
            forecastTemperatureText(
                canvas,
                true,
                summary.minimumTemperature
            ),
            FIRST_VALUE_X,
            temperatureBaseline
        );
        canvas.drawText(
            Spleen12x24,
            forecastTemperatureText(
                canvas,
                true,
                summary.maximumTemperature
            ),
            SECOND_VALUE_X,
            temperatureBaseline
        );
    }
    String rainText = String(WeatherSymbols::RAIN) +
        " 0.0mm 0% --:--";
    if (summary.rainExpected)
    {
        rainText = String(WeatherSymbols::RAIN) + " " +
            String(summary.firstRainMillimeters, 1) +
            "mm " + String(summary.firstRainProbability) + "% " +
            clockText(summary.firstRainHour, 0);
    }
    canvas.drawText(
        detailed ? Spleen12x24 : Spleen8x16,
        rainText,
        WEATHER_CONTENT_LEFT,
        rowTop + (detailed ? 125 : 61)
    );
}

void renderDashboard()
{
    SpleenCanvas canvas(displayBuffer);
    canvas.clear();
    canvas.setClipRect(
        WEATHER_PANEL_LEFT,
        0,
        WEATHER_PANEL_WIDTH,
        canvas.height()
    );
    canvas.drawRect(
        WEATHER_PANEL_LEFT,
        0,
        WEATHER_PANEL_WIDTH,
        canvas.height(),
        COLOR_BLACK
    );
    canvas.drawFastHLine(
        WEATHER_PANEL_LEFT,
        CURRENT_ROW_BOTTOM,
        WEATHER_PANEL_WIDTH,
        COLOR_BLACK
    );
    canvas.drawFastHLine(
        WEATHER_PANEL_LEFT,
        TODAY_ROW_BOTTOM,
        WEATHER_PANEL_WIDTH,
        COLOR_BLACK
    );

    const time_t now = time(nullptr);
    tm localTime = {};
    localtime_r(&now, &localTime);
    char dateBuffer[16] = {};
    char timeBuffer[8] = {};
    strftime(dateBuffer, sizeof(dateBuffer), "%d.%m.%Y", &localTime);
    strftime(timeBuffer, sizeof(timeBuffer), "%H:%M", &localTime);

    const String date = String(WEEKDAYS[localTime.tm_wday]) + ", " + dateBuffer;
    canvas.drawText(Spleen8x16, date, WEATHER_CONTENT_LEFT, 16);
    canvas.drawRightAlignedText(
        Spleen12x24,
        timeBuffer,
        WEATHER_CONTENT_RIGHT,
        20
    );

    if (!fatalError.isEmpty())
    {
        canvas.drawText(
            Spleen12x24,
            "FEHLER",
            WEATHER_CONTENT_LEFT,
            51
        );
        canvas.drawText(
            Spleen8x16,
            fatalError,
            WEATHER_CONTENT_LEFT,
            77
        );
        return;
    }

    if (haveLocalWeather)
    {
        canvas.drawText(
            Spleen16x32,
            String(WeatherSymbols::THERMOMETER) + " " +
                temperatureText(localWeather.temperature),
            WEATHER_CONTENT_LEFT,
            56
        );
        canvas.drawRightAlignedText(
            Spleen16x32,
            conditionSymbol(localWeather.condition),
            WEATHER_CONTENT_RIGHT,
            56
        );
    }
    else
    {
        canvas.drawText(
            Spleen12x24,
            "KEINE LOKALDATEN",
            WEATHER_CONTENT_LEFT,
            53
        );
    }
    drawSummary(
        canvas,
        forecast.today,
        TODAY_ROW_TOP,
        "HEUTE",
        &forecast.todaySolarTimes,
        true
    );
    drawSummary(
        canvas,
        forecast.tomorrow,
        TOMORROW_ROW_TOP,
        "MORGEN",
        nullptr,
        false
    );
}

bool refreshDisplayFull()
{
    Serial.println("Display: Fast-Mode initialisieren");
    if (!display.initializeFastMode())
    {
        Serial.println("FEHLER: BUSY-Timeout bei der Initialisierung");
        return false;
    }
    display.clearDisplayMemory();
    Serial.println("Display: vollständigen Löschzyklus starten");
    if (!display.updateFull() || !display.initializeFastMode())
    {
        Serial.println("FEHLER: BUSY-Timeout beim Löschzyklus");
        return false;
    }
    if (!display.writeFramebuffer(displayBuffer))
    {
        Serial.println("FEHLER: Framebuffer konnte nicht übertragen werden");
        return false;
    }
    Serial.println("Display: Wetterbild aufbauen");
    if (!display.updateFast())
    {
        Serial.println("FEHLER: BUSY-Timeout beim Bildaufbau");
        return false;
    }
    return true;
}

bool refreshDisplayMinute()
{
    Serial.println("Display: Vollbild-Fast-Update initialisieren");
    if (!display.initializeFastMode())
    {
        Serial.println("FEHLER: BUSY-Timeout bei der Initialisierung");
        return false;
    }
    if (!display.writeFramebuffer(displayBuffer))
    {
        Serial.println("FEHLER: Framebuffer konnte nicht übertragen werden");
        return false;
    }
    if (!display.updateFast())
    {
        Serial.println("FEHLER: BUSY-Timeout beim schnellen Bildaufbau");
        return false;
    }
    return true;
}

void updateLocalWeather()
{
    LocalWeatherReading fresh;
    String error;
    Serial.println("HomePilot: lokale Wetterdaten abrufen");
    if (localSensor.fetch(fresh, error))
    {
        localWeather = fresh;
        haveLocalWeather = true;
        localError = "";
        Serial.printf(
            "HomePilot: %.1f C, %s\n",
            localWeather.temperature,
            conditionText(localWeather.condition).c_str()
        );
    }
    else
    {
        localError = error;
        Serial.println("FEHLER: " + error);
    }
    lastLocalAttemptMs = millis();
}

void updateOpenWeather()
{
    WeatherForecast fresh;
    String error;
    Serial.println("OpenWeather: 5-Tage-/3-Stunden-Prognose abrufen");
    if (openWeather.fetch(openWeatherKey, time(nullptr), fresh, error))
    {
        forecast = fresh;
        forecastError = "";
        Serial.printf(
            "OpenWeather: heute %u Werte, morgen %u Werte\n",
            forecast.today.sampleCount,
            forecast.tomorrow.sampleCount
        );
    }
    else
    {
        forecastError = error;
        Serial.println("FEHLER: " + error);
    }
    lastOpenWeatherAttemptMs = millis();
}

void updateAndRender(const bool includeOpenWeather)
{
    if (!connectWifi())
    {
        // Ein fehlgeschlagener Wiederaufbau darf keine enge Schleife mit
        // wiederholten vollständigen E-Paper-Aktualisierungen erzeugen.
        lastLocalAttemptMs = millis();
        if (includeOpenWeather)
        {
            lastOpenWeatherAttemptMs = millis();
        }
        localError = fatalError;
        fatalError = "";
        renderDashboard();
        includeOpenWeather ? refreshDisplayFull() : refreshDisplayMinute();
        return;
    }

    updateLocalWeather();
    if (includeOpenWeather)
    {
        updateOpenWeather();
    }
    renderDashboard();
    includeOpenWeather ? refreshDisplayFull() : refreshDisplayMinute();
}
}

void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println("CrowPanel 5.79-inch weather dashboard");
    Serial.println("HomePilot: 60 s | OpenWeather: 600 s");

    if (!psramFound() || !allocateDisplayBuffer())
    {
        Serial.println("FEHLER: PSRAM/Framebuffer nicht verfügbar");
        return;
    }
    display.begin();

    if (!loadConfiguration())
    {
        renderDashboard();
        refreshDisplayFull();
        return;
    }

    if (!connectWifi() || !synchronizeTime())
    {
        renderDashboard();
        refreshDisplayFull();
        lastTimeAttemptMs = millis();
        systemReady = true;
        return;
    }

    timeReady = true;
    updateAndRender(true);
    systemReady = true;
}

void loop()
{
    if (!systemReady)
    {
        delay(1000);
        return;
    }

    const unsigned long nowMs = millis();
    if (!timeReady)
    {
        if (nowMs - lastTimeAttemptMs < LOCAL_INTERVAL_MS)
        {
            delay(100);
            return;
        }

        lastTimeAttemptMs = nowMs;
        Serial.println("Zeit: erneuter Synchronisierungsversuch");
        if (connectWifi() && synchronizeTime())
        {
            timeReady = true;
            fatalError = "";
            updateAndRender(true);
        }
        else
        {
            renderDashboard();
            refreshDisplayMinute();
        }
        return;
    }

    if (nowMs - lastLocalAttemptMs < LOCAL_INTERVAL_MS)
    {
        delay(100);
        return;
    }

    const bool openWeatherDue =
        nowMs - lastOpenWeatherAttemptMs >= OPENWEATHER_INTERVAL_MS;
    updateAndRender(openWeatherDue);
}
