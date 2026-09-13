# 61-test-disp

Minimaler Full-Screen-Test für das Elecrow CrowPanel ESP32-S3 5,79-Zoll
E-Paper HMI, SKU `DIS08792E`.

Das Projekt basiert auf `60-test` und ergänzt die Initialisierung und
Adressierung der beiden SSD1683-Controller.

## Erwartetes Verhalten

Nach dem Upload beziehungsweise Reset:

1. wird das Display vollständig gelöscht;
2. erscheint ein Testbild mit zwei Rahmen und asymmetrischen Eckmarkierungen;
3. erscheint eine große `61` in einer einfachen Segmentdarstellung;
4. markieren zwei Linien bei x=395/396 die Controllernaht und mehrere
   horizontale Linien prüfen den Übergang zwischen beiden Controllern;
5. wechselt das Display nach dem Bildaufbau in Deep Sleep.

Der erste Löschvorgang ist ein Full Refresh. Das anschließende Testbild wird
mit Elecrows Fast-Update-Sequenz aufgebaut. Sichtbares Blinken ist normal.

## Hardware- und Speicherabbild

- sichtbare Fläche: 792 × 272 Pixel;
- Treiberbuffer: 800 × 272 Pixel, 27.200 Byte;
- sichtbare Pixel ab x=396 werden im Treiberbuffer um acht Pixel verschoben;
- die Zeichenfläche wird wie in Elecrows Beispiel um 180 Grad ausgerichtet;
- Master- und Slave-SSD1683 erhalten jeweils 400 × 272 Pixel;
- Displayversorgung: GPIO 7;
- MOSI/SCK: GPIO 11/12;
- CS/DC/RESET/BUSY: GPIO 45/46/47/48.

Die Initialisierungs-, RAM-Adressierungs- und Refresh-Sequenzen wurden aus
Elecrows offiziellem Beispielstand
`453aa9ec9ccb94bc0c91c81c68eaeef851317aee` übernommen und in eine kleine,
timeout-fähige C++-Klasse übertragen:

<https://github.com/Elecrow-RD/CrowPanel-ESP32-5.79-E-paper-HMI-Display-with-272-792/tree/master/example/arduino/Examples/5.79_Global_refresh>

## Build, Upload und Monitor

```bash
pio run -d projects/61-test-disp

pio run -d projects/61-test-disp \
  -t upload \
  --upload-port /dev/cu.wchusbserial10

pio device monitor \
  -d projects/61-test-disp \
  --port /dev/cu.wchusbserial10 \
  --baud 115200
```

Der Gerätename kann sich nach erneutem Anschließen ändern. Vor weiteren
Uploads muss der serielle Monitor geschlossen werden.

## Fehlerdiagnose

Jeder Displayabschnitt wird vor seiner Ausführung seriell ausgegeben. Falls
BUSY länger als 30 Sekunden aktiv bleibt, meldet das Programm den betroffenen
Schritt, statt unbegrenzt zu warten.
