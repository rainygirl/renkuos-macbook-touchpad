# Build

Use a separate RenkuOS source checkout at
`e66eeecd57a93afd573c15cdf78d788e68d6c09d` with the sibling
`macbook-fnkey/macbook71-function-keys.patch` applied first (also bundled as
`package/prerequisites/macbook71-function-keys.patch`). Then apply
`macbook-touchpad.patch`. Build `jam -q -j4 usb_hid i2c_hid` in the configured
x86_64 build directory. This also retains the previous function-key changes.

Run `sh package/build.sh /path/to/rebuilt/usb_hid` on the matching x86_64
RenkuOS system. The package is a system package with newer file timestamps,
an exact base-system dependency and a dependency on macbook_fnkey 1.0.0.
Removing it restores the preceding USB HID module. Future macbook_fnkey
builds that replace USB HID must be coordinated with this package.

Host tests:

```sh
c++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  tests/test_motion.cpp -o /tmp/macbook-touchpad-test
/tmp/macbook-touchpad-test
python3 tests/test_descriptor.py
python3 tests/test_scroll_policy.py
```

Do not overwrite a manual non-packaged USB HID override without identifying
and backing it up. Such an override takes precedence over packaged files.

The build script now produces 0.2.1-1 with scrolling. The new locally installed build is in `dist/0.2.1/`; top-level `dist/`
artifacts describe the historical 0.1.0 release. Rebuild `usb_hid`
before packaging, then install the new hpkg and reboot the target.

0.2.1 reverses scrolling for every USB HID mouse on MacBook7,1. The shared
handler also compiles into i2c_hid, which is build-checked but not packaged
for this USB-only target. Copy MouseProtocolHandler.* to hid_shared, and
HIDDevice.* / MacBookTouchpad.h to usb_hid when using the source snapshots.
