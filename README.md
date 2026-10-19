# ESP32 Watering Controller

Scheduled watering for up to 4 pumps. It survives power cuts and has a password-protected website on the home Wi-Fi.

Spec: [docs/writing-block.md](docs/writing-block.md) · Progress: [docs/TODO.md](docs/TODO.md)

---

## Step 1 — Get the tools

You need **Docker** (to build) and **esptool** (to flash). ESP-IDF itself runs inside Docker, so you don't install it.

```sh
docker --version                  # check Docker is installed
pip install esptool               # flashing tool
docker pull espressif/idf:v5.3.2  # ESP-IDF build image (~3 GB, one time)
```

Allow your user to access the USB serial port (one time, then log out and back in):

```sh
sudo usermod -aG dialout $USER
```

> Already have ESP-IDF v5.2+ installed? You can run the `idf.py` commands directly instead of through Docker.

## Step 2 — Wire the hardware

Parts list, wiring diagram and relay/RTC warnings: [docs/HARDWARE.md](docs/HARDWARE.md).

| Signal | ESP32 GPIO |
|---|---|
| Relay 1 (Pump 1) | 26 |
| Relay 2 (Pump 2) | 27 |
| Relay 3 (Pump 3) | 25 |
| Relay 4 (Pump 4) | 33 |
| DS3231 SDA | 21 |
| DS3231 SCL | 22 |
| DS3231 VCC / GND | 3.3V / GND |

- Put a CR2032 coin cell in the DS3231. It keeps the time during power cuts.
- **Do not connect pump power yet.** First confirm in Step 6 that the relays stay OFF at boot.

## Step 3 — Configure

Open the settings menu from the project folder:

```sh
cd ~/me/watering
docker run --rm -it -v "$PWD":/project -w /project -u "$(id -u):$(id -g)" -e HOME=/tmp \
  espressif/idf:v5.3.2 idf.py menuconfig
```

Go to **Watering Controller** and set:

| Menu | Option | Set to |
|---|---|---|
| Wi-Fi | Wi-Fi SSID | Your router's network name (2.4 GHz) |
| Wi-Fi | Wi-Fi password | Your Wi-Fi password |
| Login | Initial admin password | Website password, min 8 characters |
| Pumps | Number of pumps | 1–4 |
| Pumps | Relays are active-LOW | On for most relay modules |
| Clock and power loss | Power settle delay | `10` for testing, `120` when installed |

Keys: arrow keys to move, **Enter** to open or edit, **Space** to toggle, **S** to save, **Q** to quit.

Settings are saved in `sdkconfig`. It contains your passwords: **never commit or share it.**

## Step 4 — Build

```sh
docker run --rm -v "$PWD":/project -w /project -u "$(id -u):$(id -g)" -e HOME=/tmp \
  espressif/idf:v5.3.2 idf.py build
```

It should end with `Project build complete`.

## Step 5 — Flash

Connect the ESP32 by USB (usually `/dev/ttyUSB0`), then:

```sh
cd build
python -m esptool --chip esp32 -p /dev/ttyUSB0 -b 460800 write_flash @flash_args
cd ..
```

If it doesn't connect, hold the **BOOT** button on the ESP32 while flashing starts.

## Step 6 — Check the serial log

```sh
python -m serial.tools.miniterm /dev/ttyUSB0 115200
```

Look for these lines (exit with `Ctrl+]`):

| Log line | Meaning |
|---|---|
| `connected, IP 192.168.x.x` | Wi-Fi works. Note the IP address. |
| `time from RTC` | Clock module found and its time is valid |
| `RTC time invalid` / `RTC not responding` | Time not set yet (normal on first boot) or check wiring |
| `power settled` | Settle delay finished; automatic runs are allowed |

Check that every relay stays **OFF** after reset. If a relay turns ON, change **Relays are active-LOW** (Step 3), then rebuild and reflash.

## Step 7 — Open the website

1. On the router, **reserve the IP address** for the ESP32 so it never changes.
2. On the Android phone (same Wi-Fi), open `http://<IP address>` in Chrome.
3. Log in with the initial admin password. The phone's time is sent to the controller automatically.
4. Go to **Settings** and **change the password**.
5. Chrome menu (⋮) → **Add to Home screen**, so it opens like an app.

## Step 8 — Set up pumps and schedules

1. Tap a pump → **✎** to rename it (for example "Front garden").
2. Use **Start** for 5 seconds to check that the right pump runs.
3. Under **Schedules**, tap **+ Add**: choose the start time, duration and days, then **Save**.
4. Pump 1 comes with 06:00, 14:00 and 22:00 for 30 s every day. Edit or delete them as needed.

## Step 9 — Test, then install

1. Run the acceptance tests in [docs/writing-block.md](docs/writing-block.md) §13 (TC-01 to TC-27).
2. Set **Power settle delay** back to `120` (Step 3), then build and flash again (Steps 4–5).
3. Connect pump power, mount everything in the enclosure, and check the website log after a few days of load shedding.

---

## Common tasks

| Task | How |
|---|---|
| Change Wi-Fi, pins or timings | Steps 3 → 4 → 5 |
| Change schedules or pump names | Website, no reflash |
| Forgot website password | Turn on **Login → Reset password on boot**, Steps 4–5, log in, turn it off, Steps 4–5 again |
| Run unit tests | `test/run_tests.sh` |

## Code layout

| Path | Purpose |
|---|---|
| `main/controller.c` | Pump control: schedules, queue, catch-up after power loss, manual runs |
| `main/scheduler.c` | Schedule calculations (host-tested) |
| `main/clock.c`, `rtc_ds3231.c` | Time from the DS3231 clock module, phone and internet |
| `main/store.c` | Pumps, schedules and run state in flash (NVS) |
| `main/event_log.c` | Event log stored in flash |
| `main/auth.c` | Password, sessions, login lockout |
| `main/api.c`, `web_server.c` | REST API and website server |
| `main/web/index.html` | Website (single page) |
| `main/Kconfig.projbuild` | menuconfig options |
# watering
