# Required Hardware

Parts list for the ESP32 Watering Controller 2.0. Pin assignments follow [writing-block.md](writing-block.md) §5.

Prices are rough local-market estimates (BDT). Check current prices before buying.

## 1. Controller (required)

| # | Device | Specification | Qty | Approx. price (BDT) | Notes |
|---|---|---|---|---|---|
| 1 | ESP32 development board | ESP32-WROOM-32, **4 MB flash**, USB (e.g. ESP32 DevKit V1, 30 or 38 pin) | 1 | 550–800 | Not ESP32-S2/C3: firmware and pins target the classic ESP32 |
| 2 | RTC module | DS3231 (I2C), e.g. ZS-042 board | 1 | 200–300 | Keeps time during power cuts. See [DS3231 warning](#ds3231-battery-warning) |
| 3 | Coin cell | CR2032, 3V | 1 | 30–60 | Lasts several years |
| 4 | Relay module | 1/2/4-channel, 5V coil, **optocoupler**, **JD-VCC jumper**, contacts ≥ 10A | 1 | 1-ch 120–200 · 4-ch 350–500 | One channel per pump. See [relay notes](#relay-module-notes) |
| 5 | Resistors | 10 kΩ, ¼ W | 1 per pump | 5 each | Pull-up on each relay pin (active-LOW relays) so pumps stay OFF during boot |
| 6 | Buck converter | 12V → 5V, ≥ 2A (LM2596 or MP1584 module) | 1 | 100–200 | Powers the ESP32 and relay coils from the pump supply |
| 7 | USB cable | Micro-USB or USB-C (match the board), **data** cable | 1 | 100–150 | For flashing and the serial log |

## 2. Pumps and water (per pump)

| # | Device | Specification | Qty | Approx. price (BDT) | Notes |
|---|---|---|---|---|---|
| 8 | Water pump | 12V DC diaphragm pump (e.g. 385/555 type) or 12V submersible | 1 per pump | 300–1,200 | Choose by flow (L/min) and lift height. Note the running and starting current |
| 9 | Flyback diode | 1N5819 or 1N4007 | 1 per pump | 5 | Across each DC pump's terminals (stripe to +). Protects relay contacts |
| 10 | Tubing | Matches pump outlet (usually 6–8 mm ID) | As needed | 30–60 per m | Plus drip emitters or nozzles |
| 11 | Water tank / container | — | 1 | — | Pump inlet must stay under water |

## 3. Power (required)

| # | Device | Specification | Qty | Approx. price (BDT) | Notes |
|---|---|---|---|---|---|
| 12 | Power supply | 12V DC adapter or SMPS. Current ≥ (largest pump's running current × 2) + 1A | 1 | 400–900 | Example: 2A pump → 12V **5A** supply. Covers pump start surge and the ESP32 |
| 13 | Fuse + holder | Inline blade or glass fuse, ≈ 1.5 × pump running current | 1 per pump | 50–100 | On the + wire to each pump |
| 14 | DC barrel jack / terminal | Matches the supply | 1 | 30–50 | |

## 4. Enclosure and wiring (required)

| # | Device | Specification | Qty | Approx. price (BDT) | Notes |
|---|---|---|---|---|---|
| 15 | Enclosure | Plastic, IP65 or better, ~150 × 100 × 70 mm | 1 | 300–700 | **Not metal**: it blocks Wi-Fi |
| 16 | Cable glands | PG7 / PG9 | 3–6 | 20–40 each | Keeps water out where cables enter |
| 17 | Terminal blocks | Screw type, 2–3 pin | 4–8 | 10–20 each | For pump and power wires |
| 18 | Wire | 22–24 AWG (signals), 18 AWG (pump power) | As needed | — | Red/black for power |
| 19 | Jumper wires / perfboard | Female–female jumpers or a soldered perfboard | 1 set | 100–200 | Perfboard is more reliable long term |

## 5. Tools

| Tool | Use |
|---|---|
| Multimeter | Check 5V/12V rails, relay polarity, continuity |
| Soldering iron + solder | Pull-up resistors, diodes, perfboard |
| Small screwdrivers | Terminal blocks |
| Computer with USB | Flashing (see [README](../README.md)) |

## 6. Optional

| Device | Purpose |
|---|---|
| 12V DC mini UPS (router type) | Keeps the ESP32 and pumps running during load shedding (future enhancement) |
| Status LED + 330 Ω resistor | Visual status (future enhancement) |
| Float switch | Dry-run protection (future enhancement) |

## Wiring overview

```text
12V supply ──┬── fuse ── relay COM ─(NO)── pump + ── pump − ── 12V GND
             │                           (diode across pump: stripe to +)
             └── buck 12V→5V ──┬── ESP32 5V/VIN
                               └── relay JD-VCC (coil power)

ESP32 3.3V ── relay VCC (opto side, JD-VCC jumper removed)
ESP32 GPIO 26/27/25/33 ── relay IN1–IN4   (10 kΩ pull-up from each IN to 3.3V)
ESP32 GPIO 21 ── DS3231 SDA
ESP32 GPIO 22 ── DS3231 SCL
ESP32 3.3V / GND ── DS3231 VCC / GND
All GND connected together (12V GND, buck GND, ESP32 GND)
```

Use the relay's **NO** (normally open) contact so the pump is OFF whenever the relay is unpowered.

## Relay module notes

- Most cheap modules are **active-LOW** (input LOW = relay ON). Keep **Relays are active-LOW** on in menuconfig, or turn it off for active-HIGH modules.
- 5V relay modules driven from 3.3V GPIO can stay ON or chatter. Fix: remove the **JD-VCC jumper**, power **VCC from 3.3V** (input side) and **JD-VCC from 5V** (coil side). Modules sold as "3.3V trigger" also work.
- Before connecting pumps: power the board, press reset, and confirm every relay stays OFF (no click, LED off).

## DS3231 battery warning

The common **ZS-042** DS3231 board has a charging circuit meant for rechargeable LIR2032 cells. With a normal **CR2032** it tries to charge the non-rechargeable cell, which can make it swell or leak. Do one of:

- Remove the 200 Ω resistor (or the diode) next to the battery holder on the board, **or**
- Use an LIR2032 rechargeable cell instead.

## Mains (220V AC) pumps

This list assumes **12V DC pumps**. For a 220V AC pump, use a relay or contactor rated for the pump's current and inductive load, add a proper earth, an MCB/RCD, and an enclosure suitable for mains wiring. Have a licensed electrician do the mains wiring.

## Shopping summary

| Setup | Main items |
|---|---|
| 1 pump | ESP32, DS3231 + CR2032, 1-ch relay, 1 pump, 12V supply, buck converter, fuse, diode, 10 kΩ resistor, enclosure, tubing |
| 4 pumps | Same, with a 4-ch relay, 4 pumps, 4 fuses, 4 diodes, 4 resistors. One pump runs at a time by default, so the supply is sized for the largest single pump |
