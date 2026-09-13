# 64-font-tune

Coverage-Threshold-Test für den Meslo-Bitmapfont auf dem Elecrow CrowPanel
ESP32-S3 5,79-Zoll E-Paper HMI, SKU `DIS08792E`.

Das Projekt ist eine Kopie von `63-disp-meslo`. Displaytreiber,
Framebufferabbildung, Ausrichtung und Layout bleiben unverändert. Nur die
Rasterisierung der Meslo-Glyphen wurde von direktem Monochrommodus auf
Graustufen-Coverage mit anschließender 1-Bit-Schwelle umgestellt.

Das Display erhält weiterhin ausschließlich einen monochromen 1-Bit-
Framebuffer. Die Graustufen existieren nur während der Fontgenerierung auf
dem Entwicklungsrechner.

## Aktuelle Parameter

Die vier frei veränderbaren Schwellwerte stehen in `font-thresholds.ini`:

```ini
[coverage]
size_16 = 94
size_20 = 100
size_24 = 112
size_36 = 118
```

Ein kleinerer Wert erzeugt dickere Glyphen, weil Pixel mit geringerer
Flächenabdeckung übernommen werden. Ein größerer Wert erzeugt dünnere
Glyphen. Der erlaubte Bereich ist 0 bis 255.

## Parameter ändern und Font neu erzeugen

1. Gewünschte Werte in `font-thresholds.ini` eintragen.
2. Den Fontheader neu erzeugen:

```bash
python3 projects/64-font-tune/tools/generate_meslo_fonts.py \
  "$HOME/Library/Fonts/MesloLGS NF Regular.ttf" \
  projects/64-font-tune/src/MesloFontData.h
```

3. Anschließend Projekt bauen und hochladen.

Das Skript benötigt Python 3 und Pillow. Es prüft, dass alle Werte zwischen
0 und 255 liegen. Die erzeugte Datei `src/MesloFontData.h` dokumentiert die
tatsächlich verwendeten Werte und stellt sie dem Testbild als Konstanten zur
Verfügung. Daher zeigen die linken Beschriftungen automatisch beispielsweise
`20 PX / T100` an.

## Erwartetes Bild

Das Bild entspricht Projekt 63, verwendet aber die neuen Coverage-Schwellen:

- 36 Pixel mit Schwelle 118;
- 24 Pixel mit Schwelle 112;
- 20 Pixel mit der bereits lokal angepassten Schwelle 100;
- 16 Pixel mit der bereits lokal angepassten Schwelle 94;
- deutsche Umlaute und `ß`;
- Zug-, Bus-, Uhr-, WLAN- und Warnsymbol;
- doppelter Rahmen und Uhrsymbol über der Controllernaht.

Besonders die Buchstaben `n`, `u` und `t` in `Minuten` erlauben einen direkten
Vergleich mit Projekt 63.

## Build, Upload und Monitor

```bash
pio run -d projects/64-font-tune

pio run -d projects/64-font-tune \
  -t upload \
  --upload-port /dev/cu.wchusbserial10

pio device monitor \
  -d projects/64-font-tune \
  --port /dev/cu.wchusbserial10 \
  --baud 115200
```

Der Gerätename kann sich nach erneutem Anschließen ändern. Vor dem Upload
muss ein bereits laufender serieller Monitor geschlossen werden.

Bei Erfolg endet die Ausgabe mit:

```text
Font-Tuning abgeschlossen; Display ist im Deep Sleep.
```
