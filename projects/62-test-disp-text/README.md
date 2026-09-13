# 62-test-disp-text

Text- und Ausrichtungstest fuer das Elecrow CrowPanel ESP32-S3 5,79-Zoll
E-Paper HMI, SKU `DIS08792E`.

Das Projekt ist eine Kopie des erfolgreich auf der Hardware getesteten
Projekts `61-test-disp`. Es verwendet dieselbe SSD1683-x2-Initialisierung,
dieselbe 800 x 272 Pixel grosse Treiberablage und dieselbe Abbildung der
792 x 272 sichtbaren Pixel. Neu ist eine kleine `Adafruit_GFX`-Zeichenflaeche,
die Text und GFX-Linien direkt in den PSRAM-Framebuffer schreibt.

## Erwartetes Bild

Nach dem Upload beziehungsweise Reset erscheint ein Textmuster mit:

- einem doppelten Rahmen und Beschriftungen in allen vier Randbereichen;
- der grossen Ueberschrift `CROWPANEL 5.79`;
- `TEXT RENDERING TEST` und mehreren kleineren Schriftgroessen;
- Ziffern sowie den ASCII-Umschreibungen `ae oe ue ss`;
- einem langen Satz in Schriftgroesse 1, der die Controllernaht bei
  x=395/396 ohne zusaetzliche Markierung ueberquert.

Die eingebaute Standardschrift von Adafruit GFX ist eine 5-x-7-Pixel-
Bitmap-Schrift und enthaelt keine verlaesslichen deutschen Umlaute. Deshalb
verwendet dieser Basistest nur ASCII. Eine eigene Schrift mit Umlauten kann
in einem spaeteren Test ergaenzt werden.

Vor dem Textbild fuehrt die Firmware einen vollstaendigen Loeschzyklus aus.
Sichtbares Blinken ist bei diesem E-Paper normal. Nach dem Bildaufbau wechselt
das Display in Deep Sleep; Reset startet den Test erneut, das angezeigte Bild
bleibt im stromlosen Zustand erhalten.

## Hardware- und Speicherabbild

- sichtbare Zeichenflaeche: 792 x 272 Pixel;
- Treiberbuffer im PSRAM: 800 x 272 Pixel, 27.200 Byte;
- sichtbare Pixel ab x=396 werden im Treiberbuffer um acht Pixel verschoben;
- die bestaetigte Ausrichtung aus Test 61 bleibt unveraendert;
- Displayversorgung: GPIO 7;
- MOSI/SCK: GPIO 11/12;
- CS/DC/RESET/BUSY: GPIO 45/46/47/48.

## Build, Upload und Monitor

```bash
pio run -d projects/62-test-disp-text

pio run -d projects/62-test-disp-text \
  -t upload \
  --upload-port /dev/cu.wchusbserial10

pio device monitor \
  -d projects/62-test-disp-text \
  --port /dev/cu.wchusbserial10 \
  --baud 115200
```

Der serielle Geraetename kann sich nach erneutem Anschliessen aendern. Vor
dem Upload muss ein bereits laufender serieller Monitor geschlossen werden.

Bei Erfolg endet die Ausgabe mit:

```text
Texttest abgeschlossen; Display ist im Deep Sleep.
```

Ein BUSY-Timeout wird zusammen mit dem betroffenen Initialisierungs- oder
Refresh-Schritt ausgegeben.
