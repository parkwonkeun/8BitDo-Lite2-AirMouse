# Lite 2 Control cursor bridge

This developer-session path runs on the tablet under the ADB shell identity.
The foreground monitor exclusively captures the Lite 2 S-mode mouse and
keyboard, or the D-mode gamepad, and sends their state to the monitor UI.
Outside the monitor, S-mode profiles forward motion through a virtual mouse.
RAW observes slot-navigation buttons while Android handles native motion
directly. In D mode, the bridge applies the selected gamepad bindings and both stick curves to a virtual gamepad. Other input devices remain available while monitoring.

The app reconnects over loopback with a private session token. The helper
acknowledges each profile update after installing its axis gains and response
tables. Backgrounding the editor retains its draft. Explicitly leaving the
editor restores the active profile. A lost client, failed heartbeat or physical
disconnect releases the mouse. The helper waits for a new authenticated
connection and rediscovers the physical node on each attempt. After a tablet
reboot, start a new developer session with the script below.

r13 reports left-stick offsets in REL_RX/RY and the motion source in REL_RZ
(1 for gyro, 2 for stick). The bridge consumes those fields, evaluates the saved
stick curve and integrates velocity on its own clock in approximately 4 ms steps.
A centered sample stops integration immediately; a lost stream expires after
45 ms. The optional gyro gain increases smoothly from 1x to 2x between 180
and 1500 input counts/sec and applies only to gyro reports. Wheel response is
estimated from notch intervals. On older descriptors the bridge estimates
pointer speed from displacement and elapsed time. If an r13 Bluetooth
registration still exposes only REL_X/Y/WHEEL, renew the pairing.

r14 adds two relative fields (Z and Dial) to the S-mode mouse report. Each
carries one nine-bit half of the physical button state plus one, so releases
survive the HID transport. The companion receives state changes on its private
connection. The first snapshot establishes a baseline. Slot navigation takes effect on release. S defaults to −/+ for previous/next, and D defaults to ★ for cycling profiles.

Build on Windows with Zig 0.14.1:

    zig cc -target aarch64-linux-musl -Os -Wall -Wextra -Werror -static android/bridge/lite2-pointer-bridge.c -o work/android-bridge/lite2-pointer-bridge

With the app installed and Y700 connected to USB debugging:

    ./android/bridge/start-y700.ps1 -Binary work/android-bridge/lite2-pointer-bridge

The script verifies the S-mode Linux event node and starts a new live session.
It does not reinstall the app or change the system mouse setting.
