# MX Master 3 USB dongle

ESP32-S3 firmware that bridges the MX Master 3 Mac's Bluetooth HID reports to
USB mouse and keyboard reports.

## Build and flash

```sh
source "/Users/user/.espressif/tools/activate_idf_v5.4.sh"
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodemYOUR_PORT flash monitor
```

Use the board's actual serial port. If the running HID firmware does not expose
a serial port, enter download mode: hold BOOT, press and release RESET, then
release BOOT. On boards with a separate UART connector, that connector can also
be used for flashing and logs.

The first connection may require putting the selected mouse Easy-Switch slot
into pairing mode. Subsequent connections reuse the saved bond. Do not erase
NVS when updating firmware if you want to retain pairing.

For an existing `sdkconfig`, enable `CONFIG_BT_NIMBLE_NVS_PERSIST=y` through
menuconfig; defaults apply when generating a new configuration.

## Reconnection and sleep

The Bluetooth host checks every two seconds that an idle dongle is scanning.
Connection attempts have a 30-second timeout. Disconnects release discovery
state and restart scanning; failed connection attempts and scan-start failures
are retried by the host timer. Active scanning also requests names from scan
responses, and known bonded identities can reconnect without advertising a name.
Encryption must succeed before service discovery and sequential HID subscriptions.

Mouse input during USB suspend requests remote wakeup. The host must allow USB
remote wakeup and keep the port powered. If the host removes power entirely,
the dongle boots and reconnects using its saved bond when power returns. The
input report that triggers wake is discarded; subsequent input is forwarded
once USB is ready.

This firmware retains the original MX Master 3 Mac report layout and GATT
handles (`0x2f`/`0x33`, CCCDs `0x30`/`0x34`); other mouse models or firmware
layouts may need different mappings.

## Battery reporting

After reconnecting and restoring HID subscriptions, the dongle subscribes to the
standard Bluetooth Battery Level characteristic (service `0x180f`, characteristic
`0x2a19`) before reading it, then polls once per minute as well. Notifications
update the cached percentage immediately without waking a suspended USB host.
On the tested MX Master 3 Mac, polling alone returned 100%; enabling notifications
produced 20%, matching the direct Mac connection, and subsequent reads also
returned 20%. The subscription is restored on every reconnect. Battery-read failures do not
disconnect the mouse. Unknown, malformed, or disconnected readings are not
reported as zero percent.

USB feature report 3 contains a single byte, 0–100 percent, using Generic Device
Controls / Battery Strength (`0x06/0x20`) inside the mouse collection. The USB
control request returns the cached value without blocking on Bluetooth or taking
time on the mouse's interrupt endpoint. Charging state is not inferred from the
percentage. This is a standard HID report, not Logitech HID++ emulation.

Linux's generic HID driver supports this report through `CONFIG_HID_BATTERY_STRENGTH`.
Ubuntu's UPower/desktop can use the resulting device battery; notification policy
depends on the desktop. Native battery displays and notifications on macOS and
Windows are **not guaranteed** for a generic USB HID receiver. No companion
software or drivers are installed by this project.

On Ubuntu, reconnect the USB/OTG cable after flashing, move the mouse, and run
`upower -e` followed by `upower -i DEVICE_PATH` for the new device. The underlying
reading is also available in `/sys/class/power_supply/hid-*/capacity`. Check for
`Battery notification: raw=... (N%)` or `Battery read: raw=... (N%)` in the UART logs. Test again after power-cycling the mouse.
If no Battery Level characteristic is available, the firmware logs that fact;
it does not invent a battery value.

Host-side cache tests (no board required):

```sh
cc -std=c11 -Wall -Wextra -Werror -I main main/battery_report.c tests/test_battery_report.c -o /tmp/test-dongle-battery
/tmp/test-dongle-battery
python tests/check_hid_descriptor.py build/esp-logitech-mx-master-3-usb-dongle.elf
```

The descriptor check uses ESP-IDF's Python environment. It reads the compiled
descriptor from the ELF and checks the battery usage, range, collection, report
length, and unchanged mouse/keyboard input sizes.

Compatibility reference: [Linux HID battery implementation](https://github.com/torvalds/linux/blob/v6.8/drivers/hid/hid-input.c).

## Hardware regression checks

Run these after flashing; a successful build alone does not validate radio or
host sleep behavior.

1. Pair and verify movement, both scroll wheels, buttons, and thumb gestures.
2. Turn the mouse off and on at least 20 times. Verify each reconnect completes
   service discovery and both subscriptions without exhausting the peer pool.
3. Leave the mouse idle long enough to reproduce its sleep behavior, then move
   it. It should reconnect without replugging the dongle.
4. Move the mouse out of range or turn it off while connecting. Restore it after
   the connection timeout; verify the dongle is still scanning and reconnects.
5. Sleep and resume the computer repeatedly. With USB wake enabled, try waking
   it with mouse movement; also verify input after waking it with the keyboard.
6. Unplug and reconnect the dongle. Verify the mouse reconnects without pairing
   mode. Test buttons and gestures again to confirm subscriptions were restored.

Useful logs include `disconnect; reason=`, `Connection failed`, `Connection
established`, `encryption change event`, and subscription completion statuses.
