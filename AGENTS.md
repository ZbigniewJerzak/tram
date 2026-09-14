# AGENTS.md — CrowPanel ESP32-S3 5.79-inch E-Paper

## 1. Projektziel

Dieses Repository enthält Firmware- und Lernbeispiele für ein
BHT-Summer-School-Projekt 2027. Die verbindliche Zielhardware ist das
Elecrow CrowPanel ESP32 5.79-inch E-Paper HMI Display mit ESP32-S3,
792 × 272 sichtbaren Pixeln und zwei SSD1683-Controllern.

Ziele:

- reproduzierbare Entwicklung mit VS Code und PlatformIO;
- einfache, für Schüler*innen nachvollziehbare Arduino/C++-Beispiele;
- zuverlässige Ansteuerung des E-Paper-Displays;
- schrittweiser Ausbau um Sensorik, WLAN und REST-Kommunikation.

Änderungen müssen mit der tatsächlich getesteten 5,79-Zoll-Hardware
kompatibel bleiben. Pinbelegung, Speicherabbild, Ausrichtung und
Controllersequenzen dürfen nur mit belastbarem Hardwarebeleg geändert werden.

## 2. Verifizierte Hardware

| Komponente | Verifizierte Angabe |
|---|---|
| Produkt | Elecrow CrowPanel ESP32 5.79-inch E-Paper HMI Display |
| Produkt-SKU | `DIS08792E` |
| Mikrocontroller | `ESP32-S3-WROOM-1-N8R8` |
| Flash | 8 MB; gemessen: 8.388.608 Byte |
| PSRAM | 8 MB OPI-PSRAM; gemessen nutzbar: 8.386.279 Byte |
| Display | monochromes E-Paper, Schwarz/Weiß |
| Physische Auflösung | 272 × 792 Pixel |
| Normale Ansicht | Querformat, logisch 792 × 272 Pixel |
| Display-Controller | zwei SSD1683 |
| Treiberbreite | 800 × 272 Pixel |
| Framebuffer | 27.200 Byte |
| USB–UART | USB VID:PID `1A86:7523`, Beschreibung `USB Serial` |
| Zuletzt beobachteter Port | `/dev/cu.wchusbserial110` |
| Upload/Monitor | 115200 Baud |
| Entwicklungssystem | macOS |

Der beobachtete Gerätename ist nicht dauerhaft. Nach dem Umstecken oder an
einem anderen Mac kann der Suffix anders lauten.

Für das aktuelle Projekt 70 führt der Benutzer PlatformIO aus dem Verzeichnis
`projects/` aus. Befehlsbeispiele für dieses Projekt verwenden daher
`-d ./70-spleen-wifi` und derzeit den Port `/dev/cu.wchusbserial110`.

Das PlatformIO-Board `esp32-s3-devkitc-1` ist ein kompatibles generisches
Build-Ziel und nicht die Produktbezeichnung des CrowPanels. PlatformIO kann
dieses Ziel als „No PSRAM“ beschreiben; die unten aufgeführten Build-Optionen
aktivieren den auf der realen N8R8-Hardware erfolgreich getesteten OPI-PSRAM.

Offizielle Produktseite:

<https://www.elecrow.com/crowpanel-esp32-5-79-e-paper-hmi-display-with-272-792-resolution-black-white-color-driven-by-spi-interface.html>

## 3. Verifizierte Display-Pins

Diese Belegung wurde mit den Projekten 60 bis 62 auf der realen Hardware
getestet:

```cpp
constexpr uint8_t EPD_POWER = 7;
constexpr uint8_t EPD_MOSI  = 11;
constexpr uint8_t EPD_SCK   = 12;
constexpr uint8_t EPD_CS    = 45;
constexpr uint8_t EPD_DC    = 46;
constexpr uint8_t EPD_RESET = 47;
constexpr uint8_t EPD_BUSY  = 48;
```

Randbedingungen:

- GPIO 7 schaltet die Displayversorgung und wird vor der Initialisierung auf
  `HIGH` gesetzt; anschließend 200 ms warten.
- Das Display benötigt kein MISO-Signal.
- Die Projekte 61 und 62 übertragen die Controllerdaten mit der verifizierten
  bit-basierten Taktfolge aus `ElecrowEpd579::writeBus()`.
- `EPD_BUSY` ist bereit, wenn GPIO 48 `LOW` ist.
- Alle BUSY-Wartevorgänge haben ein Timeout von 30 Sekunden.
- Diese Pins nicht ohne Schaltplan oder erfolgreichen Gegenversuch für andere
  Peripherie verwenden.

## 4. Verifizierte PlatformIO-Konfiguration

Für die 5,79-Zoll-Projekte gilt diese Basiskonfiguration:

```ini
[platformio]
default_envs = crowpanel_579

[env:crowpanel_579]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino

upload_protocol = esptool
upload_speed = 115200
monitor_speed = 115200

board_build.flash_size = 8MB
board_build.flash_mode = qio
board_build.arduino.memory_type = qio_opi
board_build.partitions = default_8MB.csv

build_flags =
    -DBOARD_HAS_PSRAM
```

Für Text und GFX-Primitiven wurde zusätzlich erfolgreich gebaut und auf der
Hardware getestet:

```ini
lib_deps =
    adafruit/Adafruit GFX Library @ 1.12.6
```

Bibliotheks-, Plattform- oder Toolchain-Versionen nur bewusst aktualisieren.
Nach einem Update mindestens Speichererkennung, vollständigen Löschzyklus,
Bildaufbau, Textdarstellung, Upload und seriellen Monitor erneut prüfen.

## 5. Displaygeometrie und Framebuffer

Die Anwendung zeichnet in normaler Betrachtungsrichtung auf eine logische
Fläche von 792 × 272 Pixeln. Der große Schriftzug `61` und der vollständige
Texttest wurden in dieser Orientierung aufrecht angezeigt.

Die beiden Controller benötigen zusammen einen 800 × 272 Pixel großen
1-Bit-Framebuffer:

```cpp
constexpr uint16_t VISIBLE_WIDTH = 792;
constexpr uint16_t DRIVER_WIDTH = 800;
constexpr uint16_t HEIGHT = 272;
constexpr size_t FRAMEBUFFER_SIZE = 800U * 272U / 8U; // 27.200 Byte
```

Der Framebuffer wird mit `ps_malloc()` im PSRAM reserviert. Ein weißer Buffer
wird vollständig mit `0xFF` gefüllt. Schwarze Pixel löschen das jeweilige Bit.

### Sichtbare Koordinaten auf Treiberkoordinaten abbilden

Zwischen den beiden sichtbaren Hälften liegt im Treiberabbild eine acht Pixel
breite, nicht sichtbare Lücke. Für jeden sichtbaren Pixel `(x, y)` gilt:

```cpp
constexpr uint16_t CONTROLLER_SEAM_X = 396;
constexpr uint16_t SEAM_ADDRESS_OFFSET = 8;
constexpr size_t DRIVER_ROW_BYTES = 800U / 8U;

const uint16_t shiftedX = x < CONTROLLER_SEAM_X ? x : x + 8;
const uint16_t driverX = 800 - shiftedX - 1;
const uint16_t driverY = 272 - y - 1;
const size_t address = driverY * DRIVER_ROW_BYTES + driverX / 8;
const uint8_t mask = 0x80U >> (driverX % 8);
```

Damit ist die Anzeige gegenüber dem rohen Controllerbuffer um 180 Grad
ausgerichtet. Die acht zusätzlichen Treiberpixel dürfen nicht als sichtbare
Spalte behandelt werden. Eine naive 792-Pixel-Abbildung erzeugt an der
Controllernaht falsche Positionen.

Master und Slave erhalten jeweils 400 × 272 Pixel beziehungsweise 13.600
Byte. `ElecrowEpd579::writeFramebuffer()` überträgt die Daten in der von
Elecrows Treiber erwarteten spaltenweisen Reihenfolge.

## 6. Verifizierter Displayablauf

Der funktionierende Ablauf aus den Projekten 61 und 62 ist:

1. Displayversorgung einschalten und GPIOs initialisieren.
2. Fast-Mode initialisieren und jeden BUSY-Zustand abwarten.
3. Master- und Slave-Controller-RAM löschen.
4. Mit `0x22`, Datenwert `0xF7`, und `0x20` einen vollständigen
   Lösch-/Refresh-Zyklus starten.
5. Fast-Mode erneut initialisieren.
6. Den 27.200-Byte-Framebuffer an Master und Slave übertragen.
7. Mit `0x22`, Datenwert `0xC7`, und `0x20` den schnellen Bildaufbau starten.
8. Mit `0x10`, Datenwert `0x01`, in Deep Sleep wechseln.

Die verwendeten RAM-Befehle sind:

| Funktion | Master | Slave |
|---|---:|---:|
| aktueller Bildspeicher | `0x24` | `0xA4` |
| zweiter Bildspeicher beim Löschen | `0x26` | `0xA6` |

Die verifizierte Implementierung liegt derzeit in:

- `projects/61-test-disp/src/ElecrowEpd579.h`
- `projects/61-test-disp/src/ElecrowEpd579.cpp`
- `projects/62-test-disp-text/src/ElecrowEpd579.h`
- `projects/62-test-disp-text/src/ElecrowEpd579.cpp`

Die Projekte 63 und 64 übernehmen diese Dateien unverändert. Projekt 63 ist
auf der Hardware bestätigt; Projekt 64 ist gebaut und wartet auf den
Hardwaretest der geänderten Font-Rasterisierung.

Sie basiert auf Elecrows offiziellem Beispielstand
`453aa9ec9ccb94bc0c91c81c68eaeef851317aee`:

<https://github.com/Elecrow-RD/CrowPanel-ESP32-5.79-E-paper-HMI-Display-with-272-792/tree/master/example/arduino/Examples/5.79_Global_refresh>

Der in Projekt 71 versuchte fensterbasierte Partial Refresh ist auf der realen
SSD1683-x2-Hardware fehlgeschlagen: Ein Fenster für die logischen Zeilen
15..101 beziehungsweise 22..110 erzeugte einen etwa 20 Pixel hohen schwarzen
Balken oben, einen großen schwarzen Balken im unteren Displaybereich und
anschließend einen ESP32-S3-Neustart. Diese Sequenz wurde aus Projekt 71 wieder
entfernt. Keine Regeln oder Wellenformen anderer Displaygrößen übernehmen. Bis
ein isoliertes Testprojekt erfolgreich ist, nur den oben dokumentierten
Full-Clear-plus-Fast-Image-Ablauf als bestätigt behandeln.

## 7. Verifizierte Teststufen

### Projekt 60: Board, PSRAM und Schnittstelle

`projects/60-test` wurde erfolgreich hochgeladen. Die serielle Ausgabe
bestätigte:

```text
Flash: 8388608 Bytes
PSRAM: 8386279 Bytes
PSRAM-Framebuffer erfolgreich angelegt.
Displayversorgung und SPI-Schnittstelle sind initialisiert.
Noch keine SSD1683-Befehle: Displayinhalt bleibt unveraendert.
```

Dass sich das E-Paper dabei nicht änderte, war korrekt: Dieser Test sendet
absichtlich keine SSD1683-Befehle.

### Projekt 61: Controller, Ausrichtung und Vollbild

`projects/61-test-disp` wurde vollständig auf der Hardware bestätigt:

- die große `61` steht in normaler Betrachtungsposition aufrecht;
- das H-artige Linienmuster überquert die Controllernaht korrekt;
- der doppelte Rahmen ist vollständig sichtbar;
- gefüllte und umrandete Eckquadrate erscheinen an den vorgesehenen Ecken;
- alle Initialisierungs-, Lösch-, Übertragungs- und Refresh-Schritte liefen
  ohne BUSY-Timeout;
- anschließend befindet sich das Display im Deep Sleep.

Die Rahmen, Eckquadrate und Linien sind absichtlich gezeichnete Testelemente
und keine Displayartefakte.

### Projekt 62: Adafruit-GFX-Text

`projects/62-test-disp-text` wurde vollständig auf der Hardware bestätigt:

- alle Texte und Schriftgrößen sind sichtbar und aufrecht;
- große und mittlere Überschriften haben korrekten Abstand zu den Linien;
- die zweite Überschrift ist horizontal zentriert, sitzt bei genauer
  Betrachtung vertikal jedoch etwas näher an der oberen Linie; dieser kleine
  Layoutversatz wird bewusst nicht korrigiert;
- Textzentrierung und Randbeschriftungen sind korrekt;
- alle Ecken sind korrekt markiert;
- der lange Text in Schriftgröße 1 überquert die Controllernaht korrekt;
- der doppelte Rahmen ist vollständig sichtbar.

Eine kurze vertikale Linie beim `f` in `fox` war kein Hardwareartefakt. Sie
stammte von zwei absichtlich gezeichneten Nahtmarkierungen bei x=395/396.
Diese Markierungen wurden entfernt; der danach getestete Text wird fehlerfrei
dargestellt.

Die große Standardschrift berührte im ersten Layout die darunterliegende
horizontale Linie. Das Layout wurde auf folgende bestätigte Positionen
korrigiert:

```cpp
drawCenteredText(canvas, "CROWPANEL 5.79", 24, 5);
canvas.drawFastHLine(20, 68, canvas.width() - 40, COLOR_BLACK);

drawCenteredText(canvas, "TEXT RENDERING TEST", 76, 4);
canvas.drawFastHLine(20, 115, canvas.width() - 40, COLOR_BLACK);
```

Adafruit GFX zeichnet über eine kleine `Adafruit_GFX`-Unterklasse direkt in
den PSRAM-Framebuffer. Dadurch wird kein zweiter Vollbildbuffer benötigt.
Für Zentrierung oder Rechtsausrichtung `getTextBounds()` verwenden und nicht
mit geschätzten Zeichenbreiten arbeiten. Die eingebaute 5×7-Pixelschrift
deckt die für den Test verwendeten ASCII-Zeichen ab; deutsche Umlaute sind in
diesem Basistest nicht verifiziert.

### Projekt 63: Meslo mit deutschen Glyphen und Symbolen

`projects/63-disp-meslo` wurde lokal gebaut und erfolgreich auf der Hardware
getestet. Das Projekt übernimmt den bestätigten Displaytreiber aus Projekt 62
unverändert und ergänzt:

- `MesloLGS NF Regular` als monochromen 1-Bit-Bitmapfont;
- drei echte Pixelgrößen: 16, 24 und 36;
- druckbares ASCII sowie `Ä Ö Ü ä ö ü ß`;
- die fünf ausgewählten Nerd-Font-Symbole Zug, Bus, Uhr, WLAN und Warnung;
- einen kleinen UTF-8-Decoder und einen Renderer für Unicode-Codepoints;
- ein mittig auf x=396 platziertes Uhrsymbol als unmarkierten Nahttest.

Nur der benötigte Font-Subset wird eingebettet, nicht die vollständige
2,5-MB-TTF-Datei. Die binären Glyphdaten aller drei Größen belegen zusammen
ungefähr 12,6 KB. Die Quelldatei für diesen Stand hatte den SHA-256-Wert
`d97946186e97f8d7c0139e8983abf40a1d2d086924f2c5dbf1c29bd8f2c6e57d`.
Die generierten Fontdaten, das reproduzierbare Pillow-Skript und Lizenzhinweise
liegen vollständig im Projekt.

Alle drei Größen, deutschen Glyphen und ausgewählten Symbole wurden korrekt
angezeigt. Bei 16 Pixeln wirken die dünnen Stämme von `n` und `u` im Wort
`Minuten` jedoch schwächer als das etwas kräftigere `t`. Bei 24 Pixeln tritt
das Problem nicht sichtbar auf. Ursache ist die direkte 1-Bit-Rasterisierung:
an der kleinen Größe werden Fontstämme je nach Lage im Pixelraster auf ein
oder zwei Pixel quantisiert.

### Projekt 64: Coverage-Threshold-Tuning

`projects/64-font-tune` wurde lokal erfolgreich gebaut; der Hardwaretest steht
noch aus. Es behält Layout, UTF-8-Renderer, Glyphauswahl und Displaytreiber von
Projekt 63 bei, rastert aber zunächst in Graustufen-Coverage und wandelt diese
danach mit größenabhängigen Schwellenwerten in 1 Bit um.

Die aktuellen, frei veränderbaren Parameter stehen in
`projects/64-font-tune/font-thresholds.ini`:

```ini
[coverage]
size_16 = 94
size_20 = 100
size_24 = 112
size_36 = 118
```

Ein kleinerer Wert übernimmt mehr teilweise bedeckte Randpixel und erzeugt
kräftigere Glyphen. Ein größerer Wert erzeugt dünnere Glyphen. Nach einer
Änderung muss `tools/generate_meslo_fonts.py` erneut ausgeführt und das Projekt
neu gebaut werden. Das Testbild zeigt die tatsächlich einkompilierten Werte
links neben jeder Schriftgröße an.

### Projekt 65: Nativer Spleen-Bitmapfont

`projects/65-font-spleen` wurde lokal gebaut und erfolgreich auf der Hardware
getestet. Der native 1-Bit-Bitmapfont wird in den drei unveränderten
Zellgrößen 8 × 16, 12 × 24 und 16 × 32 Pixel dargestellt und funktioniert auf
dem Panel sehr gut. Enthalten sind deutsche Glyphen, ausgewählte Pfeil-,
Box-Drawing- und Powerline-Zeichen sowie eigene 1-Bit-Piktogramme einschließlich
einer Tram. Die Spleen-BDF-Quellen und BSD-2-Clause-Lizenz liegen reproduzierbar
im Projekt; Pillow wird für die Generierung nicht benötigt.

### Projekt 70: Spleen, WLAN und HomePilot-Sensoren

`projects/70-spleen-wifi` wurde lokal einschließlich LittleFS-Abbild gebaut;
der Hardware- und Netzwerktest steht noch aus. Es übernimmt Displaytreiber,
Abbildung und Spleen-Renderer aus Projekt 65 unverändert, liest das
WLAN-Passwort aus LittleFS und ruft einmal beim Start die HomePilot-Sensorliste
unter `192.168.187.60` ab. Die statische ESP32-Adresse ist
`192.168.187.195/24`; die ursprünglich genannte Adresse `198.168.187.190`
würde nicht im gleichen lokalen /24-Netz liegen. Die Passwortdatei ist nur im
ignorierten Projektpfad `data/wifi-password.txt` zulässig und darf nicht
committet werden.

Die zuvor verwendete Adresse `192.168.187.190` stand in einem IP-Konflikt. Der
Konflikt verursachte sporadische DNS-Timeouts und TCP-Resets bei
OpenWeather-Verbindungen und darf für das CrowPanel nicht erneut verwendet
werden.

Der HomePilot wurde am 13. September 2026 vom Entwicklungsrechner erfolgreich
abgefragt. Die reale Antwort enthielt einen gültigen Umweltsensor mit
`temperature_primary`, `wind_speed`, `rain_detected` und `sun_brightness`
sowie vier gültige DuoFern-Rauchmelder vom Typ `32001664` mit
`smoke_detected: false`. Der niedrigste gemeldete Batteriestand betrug 44 %;
kein Gerät meldete `batteryLow`. Diese Daten passen zur implementierten
Auswahl- und Aggregationslogik. Der Test auf dem ESP32 und die Darstellung auf
dem E-Paper stehen weiterhin aus. Ein unbekannter zukünftiger Alarmwert wird
als unbekannt und nicht als sicher dargestellt.

### Projekt 71: Wetter-Dashboard mit zwei Zyklen

`projects/71-weather` wurde lokal gebaut und auf dem Display getestet. Das
Projekt trennt den lokalen
HomePilot-Zugriff, den OpenWeather-Client und den Spleen-Renderer in eigene
Module. HomePilot und Display werden jede Minute aktualisiert. Die externe
OpenWeather-Prognose wird höchstens einmal je zehn Minuten abgerufen und
dazwischen aus dem RAM-Cache angezeigt.

OpenWeathers kostenlose 5-Tage-/3-Stunden-Prognose wird für die Koordinaten
52.482781, 13.603978 abgerufen. Die Firmware gruppiert die UTC-Zeitstempel mit
der Berliner CET-/CEST-Zeitzone lokal in den Rest des heutigen Tages bis
18:00 Uhr und den gesamten morgigen Tag. Der reale API-Key wurde am
13. September 2026 erfolgreich getestet; die Antwort enthielt `cod=200` und
40 Prognosewerte. WLAN-Passwort und OpenWeather-Key liegen ausschließlich in
den ignorierten LittleFS-Quelldateien.

Der minutenweise fensterbasierte Partial Refresh wurde nach dem oben
dokumentierten Hardwarefehler entfernt. Projekt 71 verwendet zwischen den
reinigenden 10-Minuten-Zyklen wieder einen vollständigen Framebuffertransfer
mit dem bestätigten Fast-Update. Als primärer DNS-Server ist
`192.168.187.10` konfiguriert, als Fallback `192.168.187.1`; die tatsächlich
vom ESP32 übernommenen Adressen werden nach der WLAN-Verbindung seriell
ausgegeben. NTP verwendet primär `192.168.187.1`.

Spleen selbst enthält keine Wetterpiktogramme. Projekt 71 ergänzt deshalb
Thermometer (`U+1F321`), Sonne (`U+2600`), Wolke (`U+2601`), Regenwolke
(`U+1F327`), Tag (`U+1F31E`) und Nacht (`U+1F319`) aus dem monochromen
Noto-Emoji-Font. Das
Generatorskript rastert sie reproduzierbar in 16, 24 und 32 Pixel große
1-Bit-Glyphen; die Firmware enthält nur deren gepackte Bitmapdaten. Die
Noto-Quelldatei und OFL-1.1-Lizenz liegen unter
`projects/71-weather/fonts/noto-emoji/`. Diese Ergänzung ist lokal gebaut,
aber noch nicht auf dem Display bestätigt.

Die OpenWeather-Aggregation von Projekt 71 verwendet nun die überlappenden
Anteile der 3-Stunden-Werte an den vollständigen lokalen Kalendertagen heute
und morgen. Sie berechnet getrennte zeitgewichtete Tag- und Nachttemperaturen
und behält Minimalwert, Maximalwert, ersten Regenzeitraum und Anzahl der Werte
bei. `city.sunrise` und `city.sunset` werden validiert, in Berliner Stunden und
Minuten umgerechnet und als ein tägliches Paar für heute und morgen verwendet.
Angezeigt wird es nur für heute; morgen übernimmt die Uhrzeiten des vorherigen
verfügbaren Tages. Fehlt das Paar vollständig, werden keine Sonnenzeiten
angezeigt und 06:00 bis 18:00 Uhr dient für alle Tage intern als Tageszeit.
Dieser Aggregations- und Layoutstand ist lokal gebaut, aber noch nicht auf dem
Display bestätigt.

Das Layout von Projekt 71 reserviert die linken 528 × 272 Pixel vollständig
weiß. Alle Zeit-, Sensor- und Prognosedaten liegen in einem 264 Pixel breiten
Panel im rechten Drittel. Dessen drei Zeilen zeigen von oben nach unten den
aktuellen Messwert (y=0..64), die heutige Prognose (y=65..203) und die morgige
Prognose (y=204..271). Temperatur und Zustand der aktuellen Messung verwenden
beide Spleen 16 × 32; vom Zustand erscheint nur das Piktogramm, nicht das Wort
`SONNE`, `WOLKEN` oder `REGEN`. Die frühere Statuszeile
`AKTUELL | HP / SENSOR OK` entfällt. Heute zeigt Sonnenzeiten, Tag/Nacht,
Tief/Hoch, Regen und Wertezahl in Spleen 12 × 24. Sonnenaufgang wird dabei
durch ein gefülltes Dreieck nach oben, Sonnenuntergang durch ein umrandetes
Dreieck nach unten markiert. Da Spleen 12 × 24 die benötigten Unicode-Pfeile
nicht enthält, werden auch die Tief-/Hoch-Pfeile als GFX-Primitiven gezeichnet.
Morgen zeigt ausschließlich die gewichteten Tag-/Nachttemperaturen in
Spleen 12 × 24 und die Regenprognose in Spleen 8 × 16; die kleine Kopfzeile
enthält wie bei heute die Anzahl der Werte. Beide Zeilen verwenden feste
Spalten: Symbole bei x=536/660 und Werte bei x=568/692. Eine
Prognosetemperatur wird mit einer Nachkommastelle gezeigt, solange sie in das
80-Pixel-Wertefeld passt, andernfalls auf volle Grad gerundet. Der Canvas
beschränkt alle Zeichenausgaben mit einem Clipping-Rechteck auf dieses Panel.
Eine mit den tatsächlichen generierten Fontbitmaps erzeugte Vorschau liegt in
`projects/71-weather/docs/dashboard-preview.png`; die zusätzliche
Negativtemperatur-Vorschau liegt in `dashboard-preview-winter.png`. Das neue
Layout ist lokal gebaut, aber noch nicht auf der Hardware bestätigt.

### Projekt 80: BVG und Wetter

`projects/80-bvg-weather` ist eine vollständige Kopie des aktuellen
Projekt-71-Stands und ergänzt die zuvor freie linke Fläche um BVG-Daten für
die Tramhaltestelle Erich-Baron-Weg. Die linke Fläche x=0..527 zeigt zwei
Fahrtrichtungen und pro Richtung die nächsten beiden Abfahrten mit erwarteter
Uhrzeit, Verspätung, Tram-Piktogramm, Linie und Countdown in Spleen 16×32.
Ziel und aktuelle Fahrzeughaltestelle stehen dazwischen in Spleen 8×16. Die
aktuelle Haltestelle wird für die jeweils erste Abfahrt aus dem letzten zum
Abrufzeitpunkt erreichten Stopover der zugehörigen `/trips/:id`-Antwort
abgeleitet. Das Wetterpanel bleibt unverändert auf x=528..791. Beide Bereiche
werden getrennt geclippt.

Die Stop-Suche, gefilterte JSON-Auswertung, ISO-8601-Zeitumrechnung und
Sortierung stammen aus `52-bvg-departure-eink`; dessen SSD1680/GxEPD2-Treiber
und Partial-Refresh-Logik wurden ausdrücklich nicht übernommen. BVG und
HomePilot laufen gemeinsam über genau einen Minutentimer; OpenWeather behält
seinen unabhängigen 10-Minuten-Timer. Auch dieses Projekt verwendet für Minutenaktualisierungen
den vollständigen Framebuffertransfer mit Fast-Update und keinen
fensterbasierten Partial Refresh.

Firmware und LittleFS-Abbild wurden lokal erfolgreich gebaut. Die
Bitmapvorschau liegt unter
`projects/80-bvg-weather/docs/dashboard-preview.png`; der Test auf der realen
Hardware steht noch aus.

## 8. E-Paper-Verhalten

- E-Paper behält sein Bild ohne laufende Versorgung. Ein sichtbares Bild ist
  daher kein Beleg dafür, dass die aktuelle Firmware gerade Displaydaten
  sendet.
- Die BOOT-Taste verändert den Displayinhalt nicht.
- RESET startet die Firmware neu. Bei Projekt 60 bleibt das Bild unverändert,
  weil dieses Projekt keine Controllerbefehle sendet. Projekte 61 und 62
  führen nach Reset ihren Lösch- und Bildaufbau erneut aus.
- Sichtbares Blinken während des vollständigen Löschzyklus ist normal.
- Nach dem Deep-Sleep-Befehl bleibt das zuletzt aufgebaute Bild sichtbar.
- Ein doppelter Rahmen in den Tests 61 und 62 ist Teil des Testbilds, nicht die
  separate Border-Elektrode und kein Fehler.
- Keine ungeprüften Änderungen an LUT-, Border- oder Refresh-Registern
  vornehmen.

## 9. Repository- und Architekturregeln

Eine separate Firmware-Anwendung mit eigenem `setup()` und `loop()` ist ein
eigenes, vollständig baubares PlatformIO-Projekt unter `projects/`. Keine
`build_src_filter`-Workarounds verwenden.

Der aktuelle, verifizierte Entwicklungsstrang für das 5,79-Zoll-Panel ist:

```text
projects/
├── 60-test/                  # Board, Flash, PSRAM, Strom und Schnittstelle
├── 61-test-disp/             # SSD1683-x2, Vollbild und Geometrie
├── 62-test-disp-text/        # Adafruit-GFX-Text und Layout
├── 63-disp-meslo/            # Meslo, deutsche Glyphen und fünf Symbole
├── 64-font-tune/             # Größenabhängiges Coverage-Tuning
├── 65-font-spleen/           # Native Spleen-Bitmaps und Symbole
├── 70-spleen-wifi/           # WLAN, LittleFS und HomePilot-Sensoren
├── 71-weather/               # Minutenwerte plus 10-Minuten-Prognose
└── 80-bvg-weather/           # Zwei BVG-Abfahrten plus Wetter-Dashboard
```

Die vorhandene Bibliothek `common/CrowPanelSupport/` gehört nicht zum
verifizierten 5,79-Zoll-Treiberpfad und darf nicht in neue Projekte kopiert
oder verlinkt werden. Für neue Displayarbeit ist Projekt 62 die Referenz, bis
der SSD1683-x2-Treiber bewusst in eine neue gemeinsame Bibliothek verschoben
und erneut auf Hardware getestet wurde.

Allgemeine Regeln:

- `.pio/`, `.vscode/`, `.DS_Store` und Firmware-Backups nicht committen;
- Binärdumps können Konfiguration oder Zugangsdaten enthalten;
- keine WLAN-Passwörter, API-Schlüssel oder gerätespezifischen Secrets
  committen;
- bestehende Benutzeränderungen nicht überschreiben;
- Dokumentation zusammen mit relevanten Hardware- oder Konfigurationsänderungen
  aktualisieren.

Empfohlene `.gitignore`-Einträge:

```gitignore
.pio/
**/.pio/
.vscode/
**/.vscode/
.DS_Store
backups/
```

## 10. Standard-Workflow

Vor jedem Upload zuerst das Zielprojekt bauen:

```bash
pio run -d projects/62-test-disp-text
```

Firmware auf den zuletzt beobachteten Port hochladen:

```bash
pio run -d projects/62-test-disp-text \
  -t upload \
  --upload-port /dev/cu.wchusbserial10
```

Seriellen Monitor öffnen:

```bash
pio device monitor \
  -d projects/62-test-disp-text \
  --port /dev/cu.wchusbserial10 \
  --baud 115200
```

Vorher immer den aktuellen Port prüfen:

```bash
pio device list
ls -1 /dev/cu.*
```

Hinweise:

- Der Gerätename darf nicht im Quellcode vorausgesetzt werden.
- Einen laufenden seriellen Monitor vor dem Upload schließen, wenn er den
  Port blockiert.
- Falls der automatische Upload fehlschlägt: BOOT gedrückt halten, RESET kurz
  drücken und loslassen, dann BOOT loslassen und den Upload erneut starten.
- Nach einem Build muss für einen realen Displaynachweis weiterhin auf der
  Hardware hochgeladen und das Bild beobachtet werden.

## 11. Code-Konventionen

Für Lernbeispiele:

- technische Bezeichner auf Englisch;
- Erklärungen und Kommentare auf Deutsch;
- kurze Funktionen mit einer klaren Aufgabe;
- Konstanten statt unkommentierter Zahlen;
- serielle Diagnoseausgaben für relevante Zustandswechsel;
- keine unnötigen Klassenhierarchien oder Templates;
- Hardwarezugriffe kapseln, damit die Lernlogik verständlich bleibt;
- ab mehreren parallelen Aufgaben nicht blockierende Zeitsteuerung mit
  `millis()` bevorzugen.

Die Displaylogik soll Zustandswechsel vor der Ausführung ausgeben. Die
erfolgreiche Sequenz aus Test 62 lautet:

```text
CrowPanel 5.79-inch text rendering test
Display: Fast-Mode initialisieren
Display: Controller-Speicher loeschen
Display: vollstaendigen Loeschzyklus starten
Display: fuer Textbild neu initialisieren
Display: Textbild uebertragen
Display: schnellen Bildaufbau starten
Texttest abgeschlossen; Display ist im Deep Sleep.
```

## 12. Nicht verwenden oder nicht voraussetzen

Für diese Zielhardware nicht verwenden:

- GxEPD2-Treiberklassen für andere Displaymodelle;
- SSD1680-Sequenzen oder Pinbelegungen;
- SSD1306-, OLED- oder TFT-Beispiele;
- Konfigurationen anderer CrowPanel-Größen;
- Waveshare-spezifische Pins oder Wellenformen ohne Abgleich;
- einen 792 × 272 Bytebuffer ohne die acht Pixel breite Treiberlücke;
- einen einzelnen Displaycontroller;
- unbestätigte Onboard-LED-, I2C- oder Touch-Pins;
- fest codierte macOS-Gerätenamen;
- die in Projekt 71 fehlgeschlagene fensterbasierte Partial-Refresh-Sequenz;
- ungeprüfte andere Partial-Refresh- oder Hibernate-Sequenzen.

## 13. Troubleshooting

### Display bleibt unverändert

Zuerst prüfen, welche Firmware läuft. Projekt 60 initialisiert nur Versorgung
und Schnittstelle und sendet absichtlich keine SSD1683-Befehle. Außerdem
behält E-Paper den vorherigen Inhalt dauerhaft bei.

Bei Projekten 61 und 62 die serielle Ausgabe schrittweise verfolgen. Ein
30-Sekunden-BUSY-Timeout benennt den betroffenen Abschnitt.

### Board wird unter macOS nicht angezeigt

```bash
pio device list
ls -la /dev/cu.wchusbserial*
ls -la /dev/cu.usbserial*
```

Ein Gerät mit VID:PID `1A86:7523` und Beschreibung `USB Serial` wurde
erfolgreich verwendet. Erst wenn kein passender Port erscheint, CH34x-Treiber,
USB-Kabel und USB-Anschluss prüfen.

### Upload schlägt fehl

- seriellen Monitor schließen;
- Port erneut prüfen;
- Upload-Geschwindigkeit bei Bedarf reduzieren;
- BOOT/RESET-Sequenz verwenden;
- Upload erneut starten.

### PSRAM wird nicht erkannt

Die `platformio.ini` mit dem bestätigten Projekt 60 oder 62 vergleichen.
Insbesondere müssen `qio_opi` und `BOARD_HAS_PSRAM` gesetzt sein. Der
Framebuffer muss mit `ps_malloc()` angelegt werden.

### Build verhält sich inkonsistent

```bash
pio run -t clean -d projects/62-test-disp-text
pio run -d projects/62-test-disp-text
```

Nicht ohne Diagnose das gesamte PlatformIO-Verzeichnis löschen.

## 14. Firmware sichern

Vor dem Überschreiben einer unbekannten Firmware kann der vollständige
8-MB-Flash mit `esptool` gesichert werden. Backups immer unter `backups/`
speichern und niemals committen.

Beispiel, nachdem `PORT` auf den aktuell erkannten Port gesetzt wurde:

```bash
mkdir -p backups
pio pkg exec \
  --package "platformio/tool-esptoolpy" \
  -- esptool \
  --chip esp32s3 \
  --port "$PORT" \
  --baud 460800 \
  read-flash 0 ALL backups/crowpanel-factory.bin
```

Ein vollständiges 8-MB-Abbild hat 8.388.608 Byte. Der Dump enthält Binärdaten
und kann Konfiguration oder Zugangsdaten enthalten; er enthält nicht den
ursprünglichen Arduino-/C++-Quellcode.

## 15. Änderungs- und Prüfregeln für Agents

Bei Änderungen an der 5,79-Zoll-Firmware:

1. Zuerst `README.md`, `AGENTS.md`, die README des Zielprojekts sowie dessen
   `platformio.ini` und aktuellen Quellcode vollständig lesen.
2. Für Displayänderungen zusätzlich die verifizierte Implementierung in
   `projects/62-test-disp-text/src/ElecrowEpd579.*` lesen.
3. Keine Hardwareannahmen aus ähnlichen CrowPanel-, Waveshare-, OLED- oder
   TFT-Projekten übernehmen.
4. Kleine, nachvollziehbare Änderungen bevorzugen.
5. Nach Code-, Bibliotheks- oder Toolchain-Änderungen das betroffene Projekt
   lokal bauen.
6. Builds beweisen nur die Übersetzbarkeit. Displaygeometrie, Kontrast,
   Refresh und Ausrichtung müssen anschließend auf realer Hardware bestätigt
   werden.
7. Uploads oder Hardwarezugriffe nur anweisen, wenn deren Auswirkungen klar
   benannt sind.
8. Keine Zugangsdaten, WLAN-Passwörter, Binär-Backups oder Secrets committen.
9. Bereits bestätigte Pins, Geometrie, Ausrichtung und SSD1683-Sequenzen nur
   bei belastbarem Gegenbeleg ändern.
10. Unbestätigte Funktionen ausdrücklich als unbestätigt dokumentieren.
    Partial Refresh ist derzeit nicht nur unbestätigt: Der konkrete Versuch
    aus Projekt 71 ist auf der Hardware fehlgeschlagen und darf nicht erneut
    in Anwendungsprojekte übernommen werden.
11. Bei Unsicherheit den funktionierenden Stand von Projekt 62 erhalten.

## 16. Relevante Referenzen

- Elecrow CrowPanel ESP32 5.79-inch E-Paper:
  <https://www.elecrow.com/crowpanel-esp32-5-79-e-paper-hmi-display-with-272-792-resolution-black-white-color-driven-by-spi-interface.html>
- Offizielles Elecrow-Beispiel:
  <https://github.com/Elecrow-RD/CrowPanel-ESP32-5.79-E-paper-HMI-Display-with-272-792>
- Adafruit GFX:
  <https://github.com/adafruit/Adafruit-GFX-Library>
- Meslo Font:
  <https://github.com/andreberg/Meslo-Font>
- Nerd Fonts:
  <https://github.com/ryanoasis/nerd-fonts>
- Arduino Core for ESP32:
  <https://github.com/espressif/arduino-esp32>
- PlatformIO:
  <https://docs.platformio.org/>
- ESP32-S3-Dokumentation:
  <https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/>
