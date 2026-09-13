# 60-test

Minimaler Hardwaretest für das Elecrow CrowPanel ESP32-S3 5,79-Zoll
E-Paper HMI, SKU `DIS08792E`.

## Geprüfte Zielhardware

- ESP32-S3-WROOM-1-N8R8
- 8 MB Flash
- 8 MB OPI-PSRAM
- sichtbare Displayfläche: 792 × 272 Pixel
- Treiber-Framebuffer: 800 × 272 Pixel beziehungsweise 27.200 Byte
- zwei SSD1683-Controller

## Verhalten

Das Programm:

1. meldet Flash- und PSRAM-Größe über die serielle Schnittstelle;
2. prüft, ob PSRAM erkannt wurde;
3. reserviert den 27.200-Byte-Framebuffer ausdrücklich im PSRAM;
4. schaltet die Displayversorgung über GPIO 7 ein;
5. initialisiert den SPI-Bus mit SCK 12, MOSI 11 und CS 45.

Der Test sendet absichtlich noch keine Befehle an die beiden SSD1683-Controller.
Der bestehende Displayinhalt bleibt deshalb unverändert. Die Integration des
offiziellen Elecrow-Treibers ist der nächste, getrennt prüfbare Schritt.

## Build und Upload

```bash
pio run -d projects/60-test
pio run -d projects/60-test -t upload
pio device monitor -d projects/60-test
```

Der serielle Monitor arbeitet mit 115200 Baud.
