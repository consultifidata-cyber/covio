# Flash a Machine Station board

Branch `feat/station-wiegand-reader`, firmware commit `92d00c8` or newer. This build has:

- the light and siren patterns until a card is tapped (Covio D-292);
- the 10-minute siren ceiling;
- the 125 kHz Wiegand card reader.

The board is the ESP32-S3 8DI-8DO. Build environment: `esp32dev-mikiwire-station`.

## What you need

- A Windows laptop with Python 3.12 and Git.
- A USB-C data cable. Some cables only charge: if no COM port appears, try another cable.
- The board's machine name and the plant's Wi-Fi name and password.

## Steps

| #   | Do this                                                                                                                        | You should see                                                 |
| --- | ------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------- |
| 1   | `git clone https://github.com/consultifidata-cyber/covio.git covio-fw` and `cd covio-fw`                                       | The folder is created                                          |
| 2   | `git checkout feat/station-wiegand-reader` and `git log -1 --oneline`                                                          | `92d00c8 fix(station): five sound events per stop…` (or newer) |
| 3   | `pip install platformio` (once per laptop)                                                                                     | `Successfully installed platformio…`                           |
| 4   | Plug the board in by USB-C. In Device Manager → Ports, note the COM number (for example `COM5`).                               | A new `USB Serial` or `USB JTAG` port                          |
| 5   | `pio run -e esp32dev-mikiwire-station -t upload --upload-port COM5`                                                            | `[SUCCESS]` (about 3 minutes the first time)                   |
| 6   | `pio device monitor -b 115200 -p COM5`, then type `show` and press Enter                                                       | The `device_id` is printed. Write it down.                     |
| 7   | In Covio: **Devices → Setup → Assign → Add a machine**. Paste the `device_id`, name the machine, then press **Copy commands**. | "Machine added" and **On Machine Station**                     |
| 8   | In the monitor: `set wifi <name> <password>`                                                                                   | `wifi saved`                                                   |
| 9   | Paste the two copied lines (`set url …` and `set key …`)                                                                       | `server_url saved`, `api_key saved`                            |
| 10  | `reboot`, wait 30 s, then `show`                                                                                               | Your Wi-Fi, url and key, not placeholders                      |
| 11  | Continue with the Covio commissioning sheet from step 8 (`docs/runbooks/machine-station-commissioning-sheet.md`)               | "Station up to date" in Setup → Assign                         |

## If something goes wrong

- **`A fatal error occurred: Failed to connect`.** Hold the board's **BOOT** button, press **RESET** once, release **BOOT**, and run step 5 again.
- **No COM port.** Change the cable. Then install the CP210x or CH340 driver, whichever matches the board's USB chip.
- **Wi-Fi name with a space** (for example `Plant Office`) does not work with `set wifi`: rename the network, or use one without spaces.
- **Bad Wi-Fi password.** The board keeps retrying. Run `set wifi …` again with the right password, then `reboot`.
- **Station misbehaving at the machine.** In Covio, **Setup → Assign → Turn off** for that machine. The light and siren go dark within a minute, and counting carries on.

**Before any siren:** the relay fence timer must be at least the longest siren step chosen in Setup → Alerts (at most 10 minutes). See commissioning sheet step 17.
