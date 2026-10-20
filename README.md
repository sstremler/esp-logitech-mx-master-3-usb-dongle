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
