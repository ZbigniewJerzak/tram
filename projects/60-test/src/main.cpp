#include <Arduino.h>
#include <SPI.h>

#include <cstring>

namespace
{
constexpr uint8_t EPD_POWER = 7;
constexpr uint8_t EPD_MOSI = 11;
constexpr uint8_t EPD_SCK = 12;
constexpr uint8_t EPD_CS = 45;
constexpr uint8_t EPD_DC = 46;
constexpr uint8_t EPD_RESET = 47;
constexpr uint8_t EPD_BUSY = 48;

constexpr size_t VISIBLE_WIDTH = 792;
constexpr size_t DRIVER_WIDTH = 800;
constexpr size_t DISPLAY_HEIGHT = 272;
constexpr size_t FRAMEBUFFER_SIZE =
    DRIVER_WIDTH * DISPLAY_HEIGHT / 8;

uint8_t* frameBuffer = nullptr;

static_assert(DRIVER_WIDTH % 8 == 0);
static_assert(FRAMEBUFFER_SIZE == 27200);

void initializeDisplayInterface()
{
    pinMode(EPD_POWER, OUTPUT);
    digitalWrite(EPD_POWER, HIGH);
    delay(200);

    pinMode(EPD_CS, OUTPUT);
    digitalWrite(EPD_CS, HIGH);
    pinMode(EPD_DC, OUTPUT);
    pinMode(EPD_RESET, OUTPUT);
    digitalWrite(EPD_RESET, HIGH);
    pinMode(EPD_BUSY, INPUT);

    SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
}

void printMemoryInformation()
{
    Serial.printf(
        "Flash: %u Bytes\n",
        static_cast<unsigned int>(ESP.getFlashChipSize())
    );
    Serial.printf(
        "PSRAM: %u Bytes\n",
        static_cast<unsigned int>(ESP.getPsramSize())
    );
}

bool allocateFramebuffer()
{
    frameBuffer = static_cast<uint8_t*>(ps_malloc(FRAMEBUFFER_SIZE));

    if (frameBuffer == nullptr)
    {
        return false;
    }

    memset(frameBuffer, 0xFF, FRAMEBUFFER_SIZE);
    return true;
}
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("CrowPanel 5.79-inch hardware test");
    Serial.printf(
        "Visible display: %ux%u Pixel\n",
        static_cast<unsigned int>(VISIBLE_WIDTH),
        static_cast<unsigned int>(DISPLAY_HEIGHT)
    );
    Serial.printf(
        "Driver buffer: %ux%u Pixel, %u Bytes\n",
        static_cast<unsigned int>(DRIVER_WIDTH),
        static_cast<unsigned int>(DISPLAY_HEIGHT),
        static_cast<unsigned int>(FRAMEBUFFER_SIZE)
    );

    printMemoryInformation();

    if (!psramFound())
    {
        Serial.println("FEHLER: OPI-PSRAM wurde nicht erkannt.");
        return;
    }

    if (!allocateFramebuffer())
    {
        Serial.println("FEHLER: Framebuffer konnte nicht in PSRAM angelegt werden.");
        return;
    }

    initializeDisplayInterface();

    Serial.println("PSRAM-Framebuffer erfolgreich angelegt.");
    Serial.println("Displayversorgung und SPI-Schnittstelle sind initialisiert.");
    Serial.println("Noch keine SSD1683-Befehle: Displayinhalt bleibt unveraendert.");
}

void loop()
{
    delay(1000);
}
