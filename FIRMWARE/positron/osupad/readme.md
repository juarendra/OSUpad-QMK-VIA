# Positron OSUpad

A 2x3 hotswap mechanical macropad with RGB lighting, designed for rhythm gamers.

- Keyboard Maintainer: [Juarendra Ramadhani](https://github.com/juarendra)
- Hardware Supported: Positron OSUpad (STM32F103)
- Hardware Availability: [Positron Electronic](https://www.tokopedia.com/positronelectronic)

The `default` keymap is osu!-oriented:

- Keys 1/2/3: volume down, mute, volume up.
- Keys 4/6: `KC_Z`/`KC_X` for osu! gameplay.
- Key 5: unused (`KC_NO`) to avoid accidental input.

Make example for this keyboard (after setting up your build environment):

    make positron/osupad:default

Flashing example for this keyboard:

    make positron/osupad:default:flash

See the [build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) and the [make instructions](https://docs.qmk.fm/#/getting_started_make_guide) for more information. Brand new to QMK? Start with the [Complete Newbs Guide](https://docs.qmk.fm/#/newbs).

## Bootloader

Enter the bootloader using Bootmagic reset: hold the top-left key while plugging in the keyboard.
