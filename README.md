# esp-2027 — CrowPanel ESP32-S3 Firmware

Monorepository für BHT Summerschool 2027 mit dem Elecrow CrowPanel ESP32-S3 2.13" E-Paper HMI.

## Hardware

| Property | Value |
|----------|-------|
| Board SKU | `DIE01021S V1.2` |
| MCU | ESP32-S3-WROOM |
| Display | 2.13" E-Paper, 122 × 250 Pixel |
| Controller | SSD1680 |
| Flash | 8 MB |

## Verifizierte Pin-Belegung

```cpp
constexpr uint8_t EPD_POWER = 7;  // Display Power Enable
constexpr uint8_t EPD_BUSY  = 9;  // Busy Input
constexpr uint8_t EPD_RESET = 10; // Reset
constexpr uint8_t EPD_MOSI  = 11; // SPI MOSI
constexpr uint8_t EPD_SCK   = 12; // SPI Clock
constexpr uint8_t EPD_DC    = 13; // Data/Command
constexpr uint8_t EPD_CS    = 14; // Chip Select
```

**Wichtig**: GPIO 7, 9, 10, 11, 12, 13, 14 sind für das Display reserviert.

## Voraussetzungen

- VS Code
- PlatformIO IDE Extension
- CH34x Treiber (nur wenn macOS den Serial-Port nicht erkennt)

## Repository-Struktur

```
esp-2027/
├── common/CrowPanelSupport/   # Gemeinsame Hardware-Bibliothek
├── projects/                  # Unabhängige PlatformIO-Projekte
│   ├── 01-display-full-refresh/
│   ├── 02-display-partial-refresh/
│   ├── 03-deep-sleep/         # Platzhalter
│   ├── 04-wifi-rest/          # Platzhalter
│   └── 05-final-application/  # Platzhalter
├── scripts/build-all.sh       # Alle Projekte bauen
├── esp-2027.code-workspace    # VS Code Multi-Root Workspace
└── AGENTS.md                  # Agent-Anweisungen
```

## Workspace öffnen

```bash
code esp-2027.code-workspace
```

## Ein Projekt bauen

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -d projects/01-display-full-refresh
```

## Ein Projekt hochladen

```bash
pio run -d projects/01-display-full-refresh -t upload
```

## Seriellen Monitor öffnen

```bash
pio device monitor -d projects/01-display-full-refresh
```

## Alle Projekte bauen

```bash
./scripts/build-all.sh
```

## Serielle Geräte finden

```bash
pio device list
ls -1 /dev/cu.*
```

**Hinweis**: Der Gerätename (z.B. `/dev/cu.wchusbserial110`) kann zwischen Macs und USB-Anschlüssen variieren.

## CrowPanelSupport-Bibliothek

Die gemeinsame Bibliothek unter `common/CrowPanelSupport/` kapselt:

- Pin-Definitionen
- Display-Typ (`GxEPD2_213_GDEY0213B74`)
- Initialisierungsfunktionen

Jedes Projekt verlinkt diese Bibliothek per Symlink:

```ini
CrowPanelSupport=symlink://../../common/CrowPanelSupport
```

## Sicherheitshinweise

- **Keine WLAN-Passwörter oder API-Keys in Git committen**
- **Keine Flash-Dumps committen** (können Konfiguration enthalten)
- **Keine `.pio/` Build-Verzeichnisse committen**

## Troubleshooting

### PlatformIO-Befehle fehlen in VS Code

1. PlatformIO IDE Extension installiert und aktiviert?
2. `Developer: Reload Window` ausführen
3. VS Code neu starten

### Board nicht erkannt

```bash
pio device list
ls -la /dev/cu.wchusbserial*
ls -la /dev/cu.usbserial*
```

CH34x-Treiber nur installieren, wenn kein Serial-Port erscheint.

### Upload fehlgeschlagen

1. Seriellen Monitor schließen
2. Port prüfen
3. Upload-Speed reduzieren (optional)
4. BOOT/RESET-Sequenz: BOOT gedrückt halten, RESET kurz drücken, BOOT loslassen
5. Erneut uploaden

## Links

- [Elecrow CrowPanel ESP32 2.13" E-Paper](https://github.com/Elecrow-RD/CrowPanel-ESP32-2.13-E-paper-HMI-Display-with-122-250)
- [GxEPD2 Library](https://github.com/ZinggJM/GxEPD2)
- [PlatformIO](https://docs.platformio.org/)