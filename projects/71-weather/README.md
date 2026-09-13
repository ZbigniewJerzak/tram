# 71-weather

Wetter-Dashboard für das Elecrow CrowPanel ESP32-S3 5,79-Zoll E-Paper HMI.
Der erste Stand dient der vollständigen Daten- und Intervallprüfung; die
endgültige visuelle Gestaltung folgt später.

## Daten auf dem Display

- aktueller Wochentag und Datum in Berliner Ortszeit;
- aktuelle Uhrzeit als größtes Element in der Kopfzeile;
- aktuelle, lokal gemessene Außentemperatur;
- lokaler Zustand ausschließlich als großes Sonne-, Wolken- oder
  Regenpiktogramm ohne ausgeschriebenes Wort;
- Prognose für die lokalen Kalendertage `HEUTE` und `MORGEN`;
- für beide Tage getrennte, zeitlich gewichtete Temperaturen für Tag und
  Nacht;
- Minimal- und Maximaltemperatur mit Pfeilen nur für heute;
- Sonnenaufgang und Sonnenuntergang mit Uhrzeit für heute, sofern diese Daten
  in der OpenWeather-Antwort gültig vorhanden sind;
- Anzahl der überlappenden 3-Stunden-Werte nur für heute;
- beim ersten erwarteten Regenzeitraum: dessen Menge, Wahrscheinlichkeit und
  lokale Stunde; ohne Regen erscheint `0.0 mm  0%  --:--`;
- sichtbare Fehlerzustände für lokale oder externe Datenquellen.

Wind und Helligkeit des lokalen Sensors werden bewusst nicht angezeigt.

## Bildschirmaufteilung

Die linke Fläche von 528 × 272 Pixeln bleibt vollständig weiß und steht für
spätere Inhalte zur Verfügung. Sämtliche aktuellen Wetter-, Zeit- und
Prognosedaten liegen im rechten Drittel von x=528 bis x=791. Ein Clipping-
Rechteck im Renderer verhindert auch bei unerwartet langen Fehlermeldungen,
dass Pixel in die freie linke Fläche gelangen.

Das 264 Pixel breite Wetterpanel besteht aus drei übereinanderliegenden
Zellen:

| Zeile | y-Bereich | Inhalt |
|---|---:|---|
| Aktuell | 0–64 | Datum, Uhrzeit sowie Temperatur und Zustandsbild in 16×32 |
| Heute | 65–203 | kleine Kopfzeile; Sonnenzeiten, Tag/Nacht, Tief/Hoch und Regen in 12×24 |
| Morgen | 204–271 | kleine Kopfzeile; ausschließlich Tag/Nacht in 12×24 und Regen in 8×16 |

Beide Prognosezeilen zeigen ihre Wertezahl rechts in der kleinen Kopfzeile.
Die erste Datenspalte verwendet x=536 für das Symbol und x=568 für den
Wert. Die zweite Datenspalte verwendet x=660 und x=692. Damit beginnen
Sonnenaufgang, Tagestemperatur und Tiefstwert sowie Sonnenuntergang,
Nachttemperatur und Höchstwert jeweils exakt auf derselben vertikalen Linie.

Für Prognosetemperaturen stehen pro Wert 80 Pixel zur Verfügung. Passt die
Darstellung mit einer Nachkommastelle nicht hinein, wird der Wert mit
`lroundf()` auf volle Grad gerundet. Die große aktuelle Temperatur behält ihre
Nachkommastelle.

Die Vorschau wird direkt aus den für die Firmware generierten
`SpleenFontData.h`-Bitmapdaten erzeugt:

```bash
python3 projects/71-weather/tools/render_dashboard_preview.py \
  projects/71-weather/src/SpleenFontData.h \
  projects/71-weather/docs/dashboard-preview.png \
  --winter-output projects/71-weather/docs/dashboard-preview-winter.png
```

## Wetter-Piktogramme aus Noto Emoji

Die originalen Spleen-BDF-Dateien enthalten keine Wetter- oder
Tageszeitpiktogramme. Das Generatorskript rastert diese sechs Glyphen daher aus
dem offiziellen monochromen `Noto Emoji`-Font von Google und ergänzt sie in
allen drei Fontgrößen:

| Codepoint | Konstante | Bedeutung |
|---|---|---|
| `U+1F321` | `WeatherSymbols::THERMOMETER` | Thermometer |
| `U+2600` | `WeatherSymbols::SUN` | Sonne |
| `U+2601` | `WeatherSymbols::CLOUD` | Wolke |
| `U+1F327` | `WeatherSymbols::RAIN` | Wolke mit Regen |
| `U+1F31E` | `WeatherSymbols::DAY` | Tagesmittel |
| `U+1F319` | `WeatherSymbols::NIGHT` | Nachtmittel |

Die Sonnenzeiten verwenden bewusst keine Fontglyphen: Sonnenaufgang wird als
gefülltes, nach oben zeigendes Dreieck und Sonnenuntergang als umrandetes, nach
unten zeigendes Dreieck mit Adafruit-GFX-Primitiven gezeichnet.

Für die Tief-/Hoch-Markierungen werden gezeichnete Pfeile verwendet, weil der
native Spleen-12×24-Strike die entsprechenden Unicode-Pfeile nicht enthält.

Die Noto-Konturen werden mit Gewicht 600 groß gerastert, proportional in ein
quadratisches 16-, 24- oder 32-Pixel-Feld eingepasst und mit einem
Coverage-Schwellwert von 96 auf genau ein Bit pro Pixel reduziert. Die fertigen
Bits werden zusammen mit den Spleen-Zeichen in `src/SpleenFontData.h`
geschrieben. `src/WeatherSymbols.h` stellt lesbare UTF-8-Konstanten bereit;
der Renderer unterstützt dafür auch vier Byte lange UTF-8-Codepoints.

Die reproduzierbaren Quellen liegen unter `fonts/noto-emoji/`. Noto Emoji ist
unter der dort mitgelieferten SIL Open Font License 1.1 lizenziert. Zum erneuten
Generieren wird Pillow benötigt:

```bash
python3 projects/71-weather/tools/generate_spleen_fonts.py \
  projects/71-weather/fonts/upstream \
  projects/71-weather/src/SpleenFontData.h \
  --noto-emoji 'projects/71-weather/fonts/noto-emoji/NotoEmoji[wght].ttf'
```

## Zwei unabhängige Zyklen

| Zyklus | Intervall | Aktion |
|---|---:|---|
| HomePilot | 60 Sekunden | Lokalsensor und Uhrzeit aktualisieren; vollständigen Framebuffer mit Fast-Update anzeigen |
| OpenWeather | 10 Minuten | Prognose einmal abrufen, Cache ersetzen und einen reinigenden Full Refresh ausführen |

Zwischen OpenWeather-Aufrufen verwendet das Display die zuletzt erfolgreiche
Prognose weiter. Auch ein fehlgeschlagener HTTP-Versuch setzt den
10-Minuten-Timer zurück; dadurch entstehen niemals schnell aufeinanderfolgende
Wiederholungsaufrufe an OpenWeather.

Jeder OpenWeather-Aufruf protokolliert auf der seriellen Konsole Statuscode und
Dauer. Bei einem negativen `HTTPClient`-Fehler folgen die dekodierte
Fehlerbeschreibung, WLAN-Status, lokale IP, RSSI, freier Heap, letzter
TLS-Fehler sowie ein erneuter DNS-Test für `api.openweathermap.org`. URL und
API-Key werden dabei nicht ausgegeben. Zusätzlich werden die vom ESP32
tatsächlich verwendeten DNS-Adressen und ein direkter Test des kanonischen
Namens `eu-api.openweathermap.org` ausgegeben. Damit lässt sich ein Problem bei
der CNAME-Auflösung vom vollständigen Ausfall des DNS-Servers unterscheiden.

Vor jedem HTTPS-Aufruf wird der API-Hostname bis zu zweimal explizit aufgelöst.
Eine erfolgreiche Auflösung füllt zugleich den lwIP-DNS-Cache, den der direkt
anschließende `HTTPClient`-Aufruf verwendet. Scheitert HTTPS anschließend
trotzdem, prüft die Diagnose einen normalen TCP-Verbindungsaufbau zur zuvor
ermittelten Adresse auf Port 443. WLAN-Modem-Sleep ist deaktiviert, um
sporadische Verluste kurzer DNS-UDP-Antworten zu vermeiden.

Der erste Bildaufbau und jeder OpenWeather-Zyklus verwenden den bestätigten
vollständigen Löschzyklus. Dazwischen übertragen die Minutenzyklen den gesamten
Framebuffer und verwenden den bestätigten schnellen Bildaufbau. Der zuvor
implementierte fensterbasierte SSD1683-Partial-Refresh wurde entfernt: Der
Hardwaretest erzeugte schwarze Balken und unmittelbar danach einen Neustart des
ESP32-S3. Ein erneuter Partial-Refresh-Versuch gehört deshalb in ein separates
Hardwaretestprojekt und nicht in dieses Dashboard.

## Datenquellen

### Lokaler HomePilot-Sensor

Modul: `src/LocalWeatherSensor.*`

```text
http://192.168.187.60/v4/devices/?devtype=Sensor
```

Aus dem verifizierten Umweltsensor `32000064_S` werden ausschließlich diese
Felder übernommen:

- `temperature_primary`;
- `sun_detected`;
- `rain_detected`.

`rain_detected` hat Vorrang vor `sun_detected`; wenn beide falsch sind, zeigt
das Dashboard `WOLKEN`.

### OpenWeather

Modul: `src/OpenWeatherClient.*`

Verwendet wird die im kostenlosen Tarif enthaltene 5-Tage-/3-Stunden-
Prognose:

```text
https://api.openweathermap.org/data/2.5/forecast
```

Parameter:

```text
lat=52.482781
lon=13.603978
units=metric
lang=de
```

Ein einzelner API-Aufruf liefert sowohl die heute noch verfügbaren als auch
die morgigen 3-Stunden-Werte. Die Firmware schneidet jedes dreistündige
Zeitfenster an den lokalen Tagesgrenzen in Berliner CET-/CEST-Zeit ab. Dadurch
wird auch ein Wert korrekt gewichtet, dessen Zeitfenster Mitternacht
überquert. Der Forecast-5-Endpunkt liefert keine bereits vergangenen
Vorhersagewerte des heutigen Tages nach; `HEUTE` verwendet daher alle noch in
der Antwort vorhandenen Anteile des 24-Stunden-Kalendertags. `MORGEN` umfasst
den vollständigen Kalendertag.

Der reale API-Key und Endpunkt wurden am 13. September 2026 erfolgreich
getestet. OpenWeather lieferte `cod=200` und alle 40 erwarteten
3-Stunden-Einträge. Der API-Key wurde dabei nicht ausgegeben.

Als erwarteter Regenzeitraum gilt der erste Wert mit mindestens einer dieser
Bedingungen:

- `rain.3h` ist größer als null;
- die maximale Niederschlagswahrscheinlichkeit `pop` beträgt mindestens 30 %;
- die Wetter-ID gehört zu Gewitter, Nieselregen, Regen oder Schnee.

Die Temperatur ist kein einfacher Mittelwert. Jeder 3-Stunden-Wert steht für
das Zeitfenster der drei Stunden vor seinem Zeitstempel. Die Firmware gewichtet
ihn mit der Überlappungsdauer dieses Fensters und dem jeweiligen lokalen
Kalendertag. Dieses Gewicht wird zusätzlich an Sonnenaufgang und
Sonnenuntergang in Tages- und Nachtanteile zerlegt. Ein einzelner Wert kann
daher anteilig in beide Mittelwerte eingehen.

Forecast 5 liefert `city.sunrise` und `city.sunset` als UTC-Unix-Zeitstempel,
aber nur als ein stadtweites Paar und nicht als tägliche Liste. Die Firmware
wandelt dieses Paar in Berliner Ortszeiten um und verwendet dieselben
Stunden-/Minutenwerte für alle aggregierten Tage. Damit übernimmt `MORGEN`
automatisch die Sonnenzeiten des vorherigen verfügbaren Tages. Angezeigt wird
das Paar weiterhin nur bei `HEUTE`. Fehlt das Paar vollständig oder ist es
ungültig, bleiben die Uhrzeiten unsichtbar und die Berechnung verwendet für
alle Tage intern 06:00 bis 18:00 Uhr als Tag.

API-Referenz:
<https://openweathermap.org/api/forecast5>

## Zugangsdaten in LittleFS

Die beiden Dateien wurden aus `70-spleen-wifi/data/` übernommen:

```text
data/wifi-password.txt
data/openweather.txt
```

Sie werden als `/wifi-password.txt` und `/openweather.txt` in LittleFS
gespeichert. Beide Dateien sind in `.gitignore` eingetragen und dürfen nicht
committet werden. Weder Passwort noch API-Key werden seriell ausgegeben oder
auf dem Display angezeigt.

## Netzwerk und Zeit

- WLAN: `birnensaft`;
- ESP32: `192.168.187.195/24`;
- Gateway: `192.168.187.1`;
- primärer DNS-Server: `192.168.187.10`;
- DNS-Fallback: `192.168.187.1`;
- NTP primär: lokaler Server `192.168.187.1`;
- NTP-Fallbacks: `pool.ntp.org` und `time.cloudflare.com`;
- Zeitzone: Europe/Berlin mit automatischer CET-/CEST-Umschaltung.

Während der initialen NTP-Synchronisierung protokolliert die Firmware alle fünf
Sekunden Wartezeit, WLAN-Status, RSSI und den aktuellen Epoch-Wert. Bei einem
Timeout folgt eine abschließende Diagnose mit lokaler IP-Adresse. Ein Timeout
blockiert die Firmware nicht dauerhaft: WLAN und NTP werden einmal pro Minute
erneut versucht. Nach der ersten erfolgreichen Zeitsynchronisierung starten die
beiden Wetterzyklen automatisch.

## Build und Upload

Die Befehle werden aus dem Verzeichnis `projects/` ausgeführt.

```bash
pio run -d ./71-weather

pio run -d ./71-weather \
  -t uploadfs \
  --upload-port /dev/cu.wchusbserial110

pio run -d ./71-weather \
  -t upload \
  --upload-port /dev/cu.wchusbserial110
```

Monitor:

```bash
pio device monitor \
  -d ./71-weather \
  --port /dev/cu.wchusbserial110 \
  --baud 115200
```

Nach einer Änderung an einer der beiden Secret-Dateien muss `uploadfs` erneut
ausgeführt werden. Der serielle Port kann sich nach erneutem Anschließen
ändern.
