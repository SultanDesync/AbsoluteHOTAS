# AbsoluteHOTAS Key Output Reference

AbsoluteHOTAS raw custom outputs use Windows `SendInput` scan-code output. Named
ship actions do not use this table in 5.0.

Use these values in `[ButtonExpansion]` or as explicit macro targets:

```ini
iButton99 = key:0x21
iButton100 = mouse:1
```

Mouse outputs:

| Input | Output |
| --- | --- |
| Left mouse | `mouse:1` |
| Right mouse | `mouse:2` |
| Middle mouse | `mouse:3` |
| Mouse 4 / XBUTTON1 | `mouse:4` |
| Disabled | `none` |

Keyboard outputs:

| Key | Output |
| --- | --- |
| Esc | `key:0x01` |
| 1 | `key:0x02` |
| 2 | `key:0x03` |
| 3 | `key:0x04` |
| 4 | `key:0x05` |
| 5 | `key:0x06` |
| 6 | `key:0x07` |
| 7 | `key:0x08` |
| 8 | `key:0x09` |
| 9 | `key:0x0A` |
| 0 | `key:0x0B` |
| - | `key:0x0C` |
| = | `key:0x0D` |
| Backspace | `key:0x0E` |
| Tab | `key:0x0F` |
| Q | `key:0x10` |
| W | `key:0x11` |
| E | `key:0x12` |
| R | `key:0x13` |
| T | `key:0x14` |
| Y | `key:0x15` |
| U | `key:0x16` |
| I | `key:0x17` |
| O | `key:0x18` |
| P | `key:0x19` |
| [ | `key:0x1A` |
| ] | `key:0x1B` |
| Enter | `key:0x1C` |
| Left Ctrl | `key:0x1D` |
| A | `key:0x1E` |
| S | `key:0x1F` |
| D | `key:0x20` |
| F | `key:0x21` |
| G | `key:0x22` |
| H | `key:0x23` |
| J | `key:0x24` |
| K | `key:0x25` |
| L | `key:0x26` |
| ; | `key:0x27` |
| ' | `key:0x28` |
| ` | `key:0x29` |
| Left Shift | `key:0x2A` |
| Backslash | `key:0x2B` |
| Z | `key:0x2C` |
| X | `key:0x2D` |
| C | `key:0x2E` |
| V | `key:0x2F` |
| B | `key:0x30` |
| N | `key:0x31` |
| M | `key:0x32` |
| , | `key:0x33` |
| . | `key:0x34` |
| / | `key:0x35` |
| Right Shift | `key:0x36` |
| Numpad * | `key:0x37` |
| Left Alt | `key:0x38` |
| Space | `key:0x39` |
| Caps Lock | `key:0x3A` |
| F1 | `key:0x3B` |
| F2 | `key:0x3C` |
| F3 | `key:0x3D` |
| F4 | `key:0x3E` |
| F5 | `key:0x3F` |
| F6 | `key:0x40` |
| F7 | `key:0x41` |
| F8 | `key:0x42` |
| F9 | `key:0x43` |
| F10 | `key:0x44` |
| Num Lock | `key:0x45` |
| Scroll Lock | `key:0x46` |
| Numpad 7 | `key:0x47` |
| Up arrow | `key:0x48` |
| Numpad 9 | `key:0x49` |
| Numpad - | `key:0x4A` |
| Left arrow | `key:0x4B` |
| Numpad 5 | `key:0x4C` |
| Right arrow | `key:0x4D` |
| Numpad + | `key:0x4E` |
| Numpad 1 | `key:0x4F` |
| Down arrow | `key:0x50` |
| Numpad 3 | `key:0x51` |
| Numpad 0 | `key:0x52` |
| Numpad . | `key:0x53` |
| F11 | `key:0x57` |
| F12 | `key:0x58` |

Note: `key:0x48`, `key:0x50`, `key:0x4B`, and `key:0x4D` are emitted as extended arrow keys by the plugin. Use the in-game keybinding UI for Starfield's own secondary bindings when you want Numpad 8/2/4/6 instead of arrows.
