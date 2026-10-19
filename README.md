# module-I2C-picohub-MOD-0011 — noknok PicoHub

**noknok ID: MOD-0011.** The noknok PicoHub: a carrier board for the Raspberry Pi Pico 2 W (the noknok
brain) with its own power manager. Status: rev 1.0 built and brought up (October 2026); power-manager
firmware v0.3.0, updatable over I2C from the brain (noknok I2C bootloader, flash layout 3).

## What it is

- **Power in:** USB-C on J1, any charger. A plain 5 V adapter runs the board (enough for small
  products); a USB-PD charger is asked for 9 V by default and up to 20 V / 3 A (60 W, any ordinary
  cable) when the load or a mezzanine needs it.
- **Power manager:** a CH32X035 that negotiates USB-PD, switches every rail, watches input current,
  temperature and the 5 V rail, and protects the board. To the brain it is an ordinary noknok I2C
  module (type `0x07`) on bus A. The board also works with no firmware on it, at 5 V.
- **Ports:** four Qwiic / STEMMA QT compatible I2C ports on two switchable buses (J3/J4 bus A,
  J5/J6 bus B, 3.3 V), a switched 5 V USB host port (J2, USB 2.0 full speed via the Pico's PIO-USB),
  and a bottom-side four-cluster pogo-pad interface for swappable **mezzanine** boards (raw PD power,
  switched 5 V, I2C, SPI, USB).
- Single-sided assembly, flat bottom, 80 × 40 mm, no electrolytic capacitors.

## Repo contents

| Path | Contents |
| --- | --- |
| `docs/spec.md` | The specification: power tree and budgets, X035 and Pico pin maps (as built), socket overview (§5b: voltage, current and what can be switched per connector), the **mezzanine interface** (§6, frozen: pin map, geometry, design rules), key parts |
| `hardware/kicad/` | KiCad project: schematic (3 sheets) and PCB. Custom symbols and footprints are embedded; the shared noknok KiCad library is in the [Ecosystem](https://github.com/buildwithnoknok/Ecosystem) repo (`electrical/`) |
| `hardware/MOD-0011-schematic.pdf` | Schematic as PDF |
| `hardware/MOD-0011-bom.csv` | Bill of materials: designators, manufacturer part numbers, LCSC codes |
| `firmware/` | Power-manager firmware (ch32fun): source, binaries, `index.json` for updates, test and debug tools. **The I2C command map is in [`firmware/readme.md`](firmware/readme.md).** `firmware/bringup/` holds the bring-up and diagnostics firmware |
| `CHANGELOG.md` | Version history |

## Using it

- Brain pins (set in the brain's `settings.toml`): I2C bus A = GP10 (SDA) / GP11 (SCL), bus B =
  GP20 / GP21, PIO-USB = GP0 (D+) / GP1 (D−).
- The brain library `noknok.py` ([brain-Pico](https://github.com/buildwithnoknok/brain-Pico))
  finds the power manager during enumeration like any other module and offers it as `c.picohub`
  (`noknok.py` 1.16+): `status()`, `source_caps()`, `set_rail()`, `cycle_rail()`, `reset_pico()`,
  `set_min_input_voltage()`, `clear_faults()`. API: brain-Pico README → *noknok PicoHub power
  manager*; the protocol underneath: [`firmware/readme.md`](firmware/readme.md) → *I2C interface*.
- Designing a mezzanine: start with `docs/spec.md` §6. A mezzanine powered from cluster A must gate its
  load with MEZZ_EN and start safely on anything from 5 to 20 V.

## License

- Firmware / code: MIT — see [LICENSE](LICENSE).
- Hardware (schematics, PCB layout, fab files, docs): CC BY-SA 4.0 — see [LICENSE-hardware](LICENSE-hardware).
- `firmware/src/usbpd.h` is adapted from [ch32fun](https://github.com/cnlohr/ch32fun) (MIT).

## Trademarks

**noknok** is a registered trademark in Switzerland (Swissreg 1300007033).

Qwiic is a registered trademark of SparkFun Electronics, Inc. STEMMA QT is a trademark of Adafruit
Industries, LLC. Raspberry Pi and Pico are trademarks of Raspberry Pi Ltd. They are named here only to
state compatibility - noknok is not affiliated with, endorsed by, or sponsored by these companies.

## Safety & Liability

noknok hardware is an electronic device and a DIY/maker kit. You assemble, modify, flash, power, and
operate it at your own risk, and it is provided as is, without warranty. This board handles up to
20 V / 3 A. See the full notice: [License, Safety & Liability](https://buildwithnoknok.github.io/safety-and-license/).

Built with the help of AI. More about how noknok uses AI: [buildwithnoknok.github.io/safety-and-license](https://buildwithnoknok.github.io/safety-and-license/).
