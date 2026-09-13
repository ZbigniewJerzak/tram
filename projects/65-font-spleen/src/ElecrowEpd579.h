#pragma once

#include <Arduino.h>

class ElecrowEpd579
{
public:
    static constexpr uint16_t VISIBLE_WIDTH = 792;
    static constexpr uint16_t DRIVER_WIDTH = 800;
    static constexpr uint16_t HEIGHT = 272;
    static constexpr size_t FRAMEBUFFER_SIZE =
        static_cast<size_t>(DRIVER_WIDTH) * HEIGHT / 8;

    bool begin();
    bool initializeFastMode();
    bool clearDisplayMemory();
    bool writeFramebuffer(const uint8_t* frameBuffer);
    bool updateFull();
    bool updateFast();
    void deepSleep();

private:
    static constexpr uint8_t EPD_POWER = 7;
    static constexpr uint8_t EPD_MOSI = 11;
    static constexpr uint8_t EPD_SCK = 12;
    static constexpr uint8_t EPD_CS = 45;
    static constexpr uint8_t EPD_DC = 46;
    static constexpr uint8_t EPD_RESET = 47;
    static constexpr uint8_t EPD_BUSY = 48;

    static constexpr uint16_t CONTROLLER_WIDTH = 400;
    static constexpr uint16_t CONTROLLER_ROW_BYTES =
        CONTROLLER_WIDTH / 8;
    static constexpr size_t CONTROLLER_BYTES =
        static_cast<size_t>(CONTROLLER_ROW_BYTES) * HEIGHT;
    static constexpr uint32_t BUSY_TIMEOUT_MS = 30000UL;

    bool hardwareReset();
    bool waitUntilReady();
    void setMasterRamWindow();
    void setMasterRamAddress();
    void setSlaveRamWindow();
    void setSlaveRamAddress();
    void writeBus(uint8_t value);
    void writeCommand(uint8_t command);
    void writeData(uint8_t data);
};

static_assert(ElecrowEpd579::VISIBLE_WIDTH == 792);
static_assert(ElecrowEpd579::DRIVER_WIDTH == 800);
static_assert(ElecrowEpd579::HEIGHT == 272);
static_assert(ElecrowEpd579::FRAMEBUFFER_SIZE == 27200);
