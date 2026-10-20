# Research and protocol provenance

The target MacBook7,1 reports USB 05ac:0236. Its interface 2 exposes a
52-byte HID descriptor with signed 8-bit relative X/Y; interface 1 exposes
a vendor report (ID 0x44, up to 511 payload bytes). GET_REPORT 0x300 on
interface 0 returned `08 05 00 00 00 00 00 00`: mouse-emulation mode.
The existing HID mouse handler passes those relative coordinates directly.
The generic mouse input add-on already retains fractional acceleration output.
Consequently firmware-emulated movement is the leading hypothesis, not a
confirmed firmware defect. Absolute touch data bypasses that path.

## Existing reports

- [Haiku: MacBook Experiences](https://discuss.haiku-os.org/t/macbook-experiences/1681): an owner of a 2009 unibody MacBook Pro reports recognition as a mouse, without tapping or multitouch. This supports the fallback-mode observation, but does not establish the same diagonal-motion defect.
- [Haiku: Touchpad Issues](https://discuss.haiku-os.org/t/touchpad-issues/13335): touchpad behavior differing from Linux/Windows; a related report, not a MacBook7,1 diagnosis.
- [libinput touchpad jitter documentation](https://wayland.freedesktop.org/libinput/doc/latest/touchpad-jitter.html): describes suppression of movement inside a hysteresis margin. This explains a possible mechanism for small movements disappearing, but libinput is not used by this Haiku driver.
- Searches did not establish a prior Haiku MacBook7,1 report with the exact same four-axis snapping symptom. Scrolling axis-lock discussions are not evidence about pointer movement.

## Wire protocol

Primary reference: [Linux bcm5974 driver](https://github.com/torvalds/linux/blob/master/drivers/input/mouse/bcm5974.c), and [kernel BCM5974 documentation](https://www.kernel.org/doc/html/v6.12/input/devices/bcm5974.html).

Hardware facts used: Wellspring3 ANSI ID 05ac:0236; interrupt endpoint 0x81;
type-2 header length 30, integrated button at byte 15, contact record length
28; signed little-endian origin/X/Y/contact-area fields at offsets 0/2/4/16;
8-byte feature report 0x300 through interface 0, byte 0 equals 1 for raw
mode and 8 for mouse mode. These are protocol facts, not copied Linux code.
The Linux implementation is GPL-2.0; no Linux implementation was imported.
New converter and integration code are independently written MIT code.
Existing Haiku files retain their original MIT copyright notices.

## Scope and limitations

Only exact SMBIOS Apple Inc. / MacBook7,1, USB 05ac:0236, mouse interface 2
with raw endpoint 0x81 on interface 1 enables this path. It preserves the
previous macbook_fnkey changes. Single-finger pointer motion, physical
click and two-finger scrolling are supported in the 0.2.0 source. Other
multitouch gestures, tap-to-click, palm classification
and resume-time firmware-reset recovery are not implemented in this release.
A new contact anchors without motion; two contacts scroll while three or more suspend motion.
A coordinate jump larger than 1024 raw units re-anchors to avoid a teleport.
The conversion uses 16 raw units per relative count and retains both signed
remainders, without independent axis deadzones. Mouse speed/acceleration
preferences still apply downstream.

## Two-finger scrolling (0.2.0 source, 2026-10-17)

[libinput scrolling documentation](https://wayland.freedesktop.org/libinput/doc/latest/scrolling.html)
provides the gesture reference: two contacts moving vertically/horizontally.
Linux bcm5974 supplies contact data; gesture interpretation is separate.
No Linux or libinput implementation was copied. This implementation uses
contact coordinate sums (twice the centroid), independent of record order,
and accumulates fractions at 128 raw centroid units per wheel step. This is
an initial tuning choice, not a Linux calibration. It provides discrete wheel
events, without libinput's axis locking, kinetic scrolling or palm rejection.

[Haiku MouseProtocolHandler](https://github.com/haiku/haiku/blob/master/src/add-ons/kernel/drivers/input/hid_shared/MouseProtocolHandler.cpp)
accepts Generic Desktop Wheel (0x01/0x38) and Consumer AC Pan (0x0c/0x0238).
Wheel is negated into wheel_ydelta; AC Pan passes into wheel_xdelta. The
synthetic report is now 9 bytes: button, LE16 X, Y, Wheel, AC Pan. Its kernel
buffer includes three extra bytes for HIDReportItem's 32-bit extraction.
Contact count changes, zero-origin replacement markers, malformed packets,
lifts and large jumps discard accumulated movement. Hardware sensitivity,
contact reporting and actual application scroll behavior remain unverified.

## Natural scrolling (0.2.1)

The user confirmed 0.2.0 scrolling works on the target, then requested natural
scrolling for both the touchpad and ordinary mice on this MacBook. The shared
MouseProtocolHandler checks exact SMBIOS Apple Inc. / MacBook7,1 at device
creation and reverses both wheel axes only on that host. Missing SMBIOS leaves
the original behavior intact. The raw touchpad converter emits standard wheel
signs so inversion happens exactly once. This package rebuilds USB HID, covering
the built-in touchpad and external USB HID mice, including USB receivers.
Other input transports are not changed by the packaged USB HID module.
Pointer coordinates and button handling remain unchanged. No global mouse
settings or device VID/PID filters are used for the scroll preference.
