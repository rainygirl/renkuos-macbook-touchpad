# MacBook Touchpad for RenkuOS

[English](README.md) · [한국어](README.ko.md) · [日本語](README.ja.md)

Native raw-coordinate touchpad support for the 2010 MacBook7,1 (USB 05ac:0236).
Single-finger movement, tap to click, physical click, dragging with the
button held and two-finger vertical/horizontal scrolling.

## Install

```sh
pkgman add-repo https://pkgman.rainygirl.com/x86_64
pkgman refresh
pkgman install macbook_touchpad
```

Restart afterwards. Skip `add-repo` if the repository is already configured.
Haiku package names cannot contain hyphens, so the install name is
`macbook_touchpad`.

Requires x86_64 RenkuOS `r1~beta6_hrev60072_269_dirty`. `macbook_fnkey` is
installed as a dependency and its keyboard and brightness fixes are kept.
Other Mac models and USB IDs are unchanged.

## Remove

```sh
pkgman uninstall macbook_touchpad
```

Restart afterwards. The `macbook_fnkey` USB HID module becomes active again.
Neither installing nor removing restarts the system for you.

## License

MIT, see [LICENSE](LICENSE).

## AI disclosure

Parts of MacBook Touchpad were developed with the assistance of AI coding tools
(OpenAI Codex and Anthropic's Claude). All code has been reviewed and tested by
the author.
