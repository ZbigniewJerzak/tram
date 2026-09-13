#include "ElecrowEpd579.h"

bool ElecrowEpd579::begin()
{
    pinMode(EPD_POWER, OUTPUT);
    digitalWrite(EPD_POWER, HIGH);
    delay(200);

    pinMode(EPD_SCK, OUTPUT);
    pinMode(EPD_MOSI, OUTPUT);
    pinMode(EPD_RESET, OUTPUT);
    pinMode(EPD_DC, OUTPUT);
    pinMode(EPD_CS, OUTPUT);
    pinMode(EPD_BUSY, INPUT);

    digitalWrite(EPD_CS, HIGH);
    digitalWrite(EPD_SCK, HIGH);
    digitalWrite(EPD_RESET, HIGH);

    return true;
}

bool ElecrowEpd579::waitUntilReady()
{
    const unsigned long startTime = millis();

    while (digitalRead(EPD_BUSY) != LOW)
    {
        if (millis() - startTime >= BUSY_TIMEOUT_MS)
        {
            return false;
        }

        delay(1);
    }

    return true;
}

bool ElecrowEpd579::hardwareReset()
{
    delay(10);
    digitalWrite(EPD_RESET, LOW);
    delay(10);
    digitalWrite(EPD_RESET, HIGH);
    delay(10);
    return waitUntilReady();
}

void ElecrowEpd579::writeBus(uint8_t value)
{
    digitalWrite(EPD_CS, LOW);

    for (uint8_t bit = 0; bit < 8; ++bit)
    {
        digitalWrite(EPD_SCK, LOW);
        digitalWrite(EPD_MOSI, (value & 0x80U) != 0U ? HIGH : LOW);
        digitalWrite(EPD_SCK, HIGH);
        value <<= 1U;
    }

    digitalWrite(EPD_CS, HIGH);
}

void ElecrowEpd579::writeCommand(const uint8_t command)
{
    digitalWrite(EPD_DC, LOW);
    writeBus(command);
    digitalWrite(EPD_DC, HIGH);
}

void ElecrowEpd579::writeData(const uint8_t data)
{
    digitalWrite(EPD_DC, HIGH);
    writeBus(data);
}

bool ElecrowEpd579::initializeFastMode()
{
    if (!hardwareReset())
    {
        return false;
    }

    writeCommand(0x12);
    if (!waitUntilReady())
    {
        return false;
    }

    writeCommand(0x18);
    writeData(0x80);

    writeCommand(0x22);
    writeData(0xB1);
    writeCommand(0x20);
    if (!waitUntilReady())
    {
        return false;
    }

    writeCommand(0x1A);
    writeData(0x64);
    writeData(0x00);

    writeCommand(0x22);
    writeData(0x91);
    writeCommand(0x20);
    if (!waitUntilReady())
    {
        return false;
    }

    writeCommand(0x3C);
    writeData(0x03);
    return waitUntilReady();
}

void ElecrowEpd579::setMasterRamWindow()
{
    writeCommand(0x11);
    writeData(0x05);

    writeCommand(0x44);
    writeData(0x00);
    writeData(0x31);

    writeCommand(0x45);
    writeData(0x0F);
    writeData(0x01);
    writeData(0x00);
    writeData(0x00);
}

void ElecrowEpd579::setMasterRamAddress()
{
    writeCommand(0x4E);
    writeData(0x00);

    writeCommand(0x4F);
    writeData(0x0F);
    writeData(0x01);
}

void ElecrowEpd579::setSlaveRamWindow()
{
    writeCommand(0x91);
    writeData(0x04);

    writeCommand(0xC4);
    writeData(0x31);
    writeData(0x00);

    writeCommand(0xC5);
    writeData(0x0F);
    writeData(0x01);
    writeData(0x00);
    writeData(0x00);
}

void ElecrowEpd579::setSlaveRamAddress()
{
    writeCommand(0xCE);
    writeData(0x31);

    writeCommand(0xCF);
    writeData(0x0F);
    writeData(0x01);
}

bool ElecrowEpd579::clearDisplayMemory()
{
    setMasterRamWindow();
    setMasterRamAddress();
    writeCommand(0x24);
    for (size_t index = 0; index < CONTROLLER_BYTES; ++index)
    {
        writeData(0xFF);
    }

    setMasterRamAddress();
    writeCommand(0x26);
    for (size_t index = 0; index < CONTROLLER_BYTES; ++index)
    {
        writeData(0x00);
    }

    setSlaveRamWindow();
    setSlaveRamAddress();
    writeCommand(0xA4);
    for (size_t index = 0; index < CONTROLLER_BYTES; ++index)
    {
        writeData(0xFF);
    }

    setSlaveRamAddress();
    writeCommand(0xA6);
    for (size_t index = 0; index < CONTROLLER_BYTES; ++index)
    {
        writeData(0x00);
    }

    return true;
}

bool ElecrowEpd579::writeFramebuffer(const uint8_t* const frameBuffer)
{
    if (frameBuffer == nullptr)
    {
        return false;
    }

    size_t sourceColumn = 0;
    size_t sourceRow = 0;

    setMasterRamWindow();
    setMasterRamAddress();
    writeCommand(0x24);

    for (size_t index = 0; index < CONTROLLER_BYTES; ++index)
    {
        writeData(frameBuffer[sourceRow * 100U + sourceColumn]);
        ++sourceRow;

        if (sourceRow >= HEIGHT)
        {
            ++sourceColumn;
            sourceRow = 0;
        }
    }

    setSlaveRamWindow();
    setSlaveRamAddress();
    writeCommand(0xA4);

    for (size_t index = 0; index < CONTROLLER_BYTES; ++index)
    {
        writeData(frameBuffer[sourceRow * 100U + sourceColumn]);
        ++sourceRow;

        if (sourceRow >= HEIGHT)
        {
            ++sourceColumn;
            sourceRow = 0;
        }
    }

    return true;
}

bool ElecrowEpd579::updateFull()
{
    writeCommand(0x22);
    writeData(0xF7);
    writeCommand(0x20);
    return waitUntilReady();
}

bool ElecrowEpd579::updateFast()
{
    writeCommand(0x22);
    writeData(0xC7);
    writeCommand(0x20);
    return waitUntilReady();
}

void ElecrowEpd579::deepSleep()
{
    writeCommand(0x10);
    writeData(0x01);
    delay(5);
}
