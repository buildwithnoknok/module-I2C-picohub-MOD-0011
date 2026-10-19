# MOD-0011 — noknok PicoHub — Specification

**Status:** production module, rev 1.0 built and brought up in October 2026.
noknok ID: **MOD-0011**. This is **the** noknok PicoHub; it supersedes MOD-0008, an earlier passive
Pico mount that will not launch as a product.
**Author:** noknok hardware (with AI assistance). Spec v1.0, 24 Sep 2026 — matches schematic rev 0.9. **Sections 3 and 4 rewritten 18 Oct 2026 from the fabricated rev 1.0 board (DEV-117); where older text in this file names other X035 or Pico pins, sections 3 and 4 win.** Since v0.8: buck enable on PB12, host CC sense via a 47 k/47 k divider, cluster A widened to 4 × VBUS + 4 × GND, cluster B re-allocated, MEZZ_AIN dropped, EN_USB and MEZZ_EN default OFF (§5b), and the design point is now **20 V / 3 A = 60 W** with 5 A-rated parts as headroom (axiom 3).

---

## 1. What it is

A Raspberry Pi Pico / Pico 2 W carrier that is the noknok **Conductor**, with a smart power
system managed by a CH32X035 microcontroller. To the Conductor the board is an ordinary
**I2C module** ("noknok Power"): it enumerates on one of the module buses, reports the
charger contract and rail currents, and can power-cycle a bus.

Design axioms (from the discussion of 19–20 Sep 2026):

1. **Every module handles its own internals.** The hub's internals are the power system, so the
   hub owns power governance — not the Pico.
2. **The X035 is never in the critical path.** With no firmware at all the board must still work
   as a plain 5 V PicoHub: external Rd on CC, the buck and both I2C buses default ON. Ports that a
   foreign supply could drive (USB host, mezzanine 5 V) default **OFF** — decided case by case on
   24 Sep 2026, not by a blanket rule. See §5b.
3. **Maximum power in: 20 V / 3 A = 60 W** (decided 24 Sep 2026). That is a normative PD contract —
   every 60 W and 65 W charger offers it — and 3 A is the limit of an **ordinary USB-C cable**, so no
   e-marked cable is needed. A 100 W charger simply gets a 3 A request instead of its advertised 5 A.
   The hardware stays **5 A-rated** (HRO receptacle, 2920 PTC, 8-contact cluster A) as headroom, so
   nothing nuisance-trips at 3 A; only the claim and the trace widths are 3 A. Earlier revisions
   claimed 100 W, which was never real: it needed a special cable *and* F1 derates to ~3.5 A hot
   (§2.1). Board draw is ~14 W worst case, leaving ~46 W for a mezzanine.
4. **Single-sided assembly, flat bottom.** All parts on top; the bottom carries only the
   mezzanine contact pads. Cheaper assembly, housing-friendly, mezzanine-ready.
5. **No aluminium electrolytic, no tantalum** (Ecosystem electrical guideline §5). MLCC/polymer only.

---

## 2. Power tree

```
USB-C PD in "PWR" (16-pin socket, power only — data pins NC)
 ├─ CC1/CC2 ── 5.1 k Rd ×2 (external, always present) ── CH32X035 PC14/PC15 (PD PHY)
 │             ESD on CC (PESD-class, 2-line)
 ├─ VBUS ── PTC 5 A / 30 V (2920) ── TVS 24 V ── node "VBUS_PD"   [design limit 3 A = 60 W]
 │        ├─ 10 mΩ shunt ── INA180A2 ── ADC "I_IN"           (input current, high side)
 │        ├─ 100 k / 10 k divider ── ADC "V_IN"              (input voltage, 20 V → 1.82 V)
 │        ├─ 2 × 4.7 µF 50 V 1206 (sink capacitance ≤ 10 µF, USB-C rule)
 │        ├─ ME6203A33 ── "+3V3_MCU" ── CH32X035 VDD, INA180 ×N VS   (always-on, independent)
 │        ├─ Mezzanine cluster A: 4 × VBUS_PD + 4 × GND (after the shunt, so the mezzanine is metered)
 │        └─ TPS54302 buck ── "+5V" (5.0 V, 3 A)
 │              ├─ 100 k / 10 k divider ── ADC "V_5V"        (detects buck dropout on a 5 V charger)
 │              ├─ SS34 Schottky ── Pico VSYS (pin 39)        (micro-USB stays programming-only)
 │              ├─ SY6280AAC load switch (EN ← X035, Rset 4.7 k → 1.45 A) ── DUSB "USB" socket VBUS
 │              │     Rp 22 k ×2 from +5V on CC (1.5 A source); CC1/CC2 → 47k/47k → X035 PC0/PC3 (device detect, §2.4)
 │              ├─ SY6280AAC (EN_A ← X035, Rset 10 k → 0.68 A) ── AP2114H-3.3 ── "+3V3_A" ── ports 1–2 (I2C bus A)
 │              └─ SY6280AAC (EN_B ← X035, Rset 10 k → 0.68 A) ── AP2114H-3.3 ── "+3V3_B" ── ports 3–4 (I2C bus B)
 │              (per-rail current sensing dropped 21 Sep — see §2.3)
```

### 2.1 Budget

| Rail | Source | Design load | Limit |
| --- | --- | --- | --- |
| VBUS_PD | charger, PD 5/9/15/20 V | ≤ 14 W board + mezzanine | **design limit 3 A (60 W)**; parts rated 5 A, PTC holds ~3.5 A in a housing |
| +5V | TPS54302 | Pico 0.5 A pk + USB 1.5 A + LDO inputs ~0.6 A ≈ 2.6 A | 3 A (device), 5 A pk limit |
| +3V3_A / _B | AP2114H each | ~0.2 A typical (two modules per bus) | SY6280 limit **0.68 A** (Rset 10 k) = LDO thermal budget, 1.15 W in SOT-223 |
| DUSB VBUS | SY6280 | ≤ 1.45 A | Rset 4.7 k |
| +3V3_MCU | ME6203 | ~15 mA | 100 mA |

Thermal note on the LDOs: (5.0 − 3.3) V × 0.68 A = 1.15 W in SOT-223 ≈ +70 °C with a generous
copper pour — hot but within limits. Since per-rail current sensing was dropped (§2.3), the
**SY6280 limit is the only enforcement**, so it is set to 0.68 A (Rset 10 k) instead of 1 A; at 1 A
the LDO would sit at 1.7 W on its thermal shutdown. Two typical modules per bus draw ~0.15 A.

**Why 3 A and not 5 A** (24 Sep 2026): F1 was the weakest link. A PTC works by self-heating, so its hold
current is specified at 23 °C and derates roughly ×0.7 at 60 °C: inside a printed housing the
2920L500/30GR trips at about **3.3–3.5 A**, not 5 A, and at 25–50 mΩ it puts 0.6–1.2 W of its own
into the board at high current. 2920 is the smallest package that does 5 A at this voltage — 1812 and
2016 top out at 2.6 A — so there is no smaller part, and a 6 A part would be bigger. Kept deliberately
for this spin: it protects the connector, the cable and the unswitched mezzanine cluster A, and it
cannot be added after the boards exist. Changed from the /24GR to the **/30GR** (30 V) version: same
body, more headroom on the PD voltage steps, cheaper, and far better stocked. At the 3 A design point
its ~3.5 A hot hold is margin rather than a limit.

**Firmware: negotiate the lowest voltage that meets demand.** With no mezzanine present, request
**9 V** rather than 20 V: the ME6203 then drops 5.7 V instead of 16.7 V (0.07 W instead of 0.20 W)
and the buck's switching losses roughly halve — about **0.25 W less heat** in the housing, from the
two parts that run hottest for their size. Step up to 15 V or 20 V when MEZZ_PRESENT is asserted or
measured demand rises, and step back down when it falls. It also keeps the exposed cluster-A pads and
J1 at 9 V when no mezzanine is fitted.

**Inrush.** Everything on the board is soft-started (TPS54302 internally, plus the four SY6280s), so
the only uncontrolled path is a mezzanine's own bulk capacitance on cluster A, which has no switch.
Two mitigations, both free: firmware **negotiates 5 V first** and steps up only once the rails are
settled — charging the same capacitance at 5 V instead of 20 V is 16× gentler, since energy scales
with V² — and the mezzanine rules (§6) require a mezzanine to limit its own inrush.

Plain 5 V charger: the buck cannot make 5.0 V from 5.0 V and drops out to ~4.6–4.8 V.
Everything downstream tolerates this (Pico VSYS ≥ 1.8 V, USB device ≥ 4.4 V, LDO dropout
0.45 V typ / 0.75 V max → 3.3 V still met). `V_5V` lets the firmware see it. **Measure on the bench.**

### 2.3 Current sensing — input only (decision 21 Sep 2026)

Only the **input** is metered (R3 + U4 → `I_IN`, plus `V_IN`): that is the number governance needs
("12 W of the 15 W this charger gives"). The three per-rail sensors (USB port, bus A, bus B) were
dropped: the SY6280 limits already protect the hardware, a stuck I2C module is detected by I2C
errors (and cured by EN_A/EN_B), and a USB module meters itself (LEDs 16x reads its own CC/VBUS)
— "every module handles its own internals". Saves 12 parts, three 2512 shunts and three ADC pins.
The mezzanine likewise meters itself if it wants to (it has MEZZ_FAULT to raise a fault instantly; the analog line MEZZ_AIN was dropped on 24 Sep when cluster A was widened, freeing an X035 ADC pin; as built, PA5 carries I_IN).

### 2.4 USB host port detection (decision 21 Sep 2026)

R19/R20 (22 k Rp) pull the host port's CC1/CC2 up to **+5V** (before the switch), and each CC line
reaches the X035 on PC0/PC3 (A10/A13) through a **halving divider: 47 k series (R27, R28) + 47 k to GND
(R35, R36), 1 nF at the pin (C36, C37)** — revised 24 Sep 2026 on the engineer's review.

The divider is mandatory: X035 I/Os are **not 5 V tolerant** (abs max VDD + 0.3 V) and an unattached
CC sits at 5 V, so the original 10 k series resistor was injecting ~140 µA into the pin's ESD diode.
It is deliberately high-impedance — 94 k across CC barely loads the 22 k Rp, so an attached sink
still reads 0.90 V and stays inside the 1.5 A band (0.66–1.23 V). A 5.1 k / 5.1 k divider would pull
that to 0.67 V, right on the boundary, and a device could fall back to 500 mA.

What the ADC sees: **2.03 V** empty socket, **0.45 V** device attached (Rd 5.1 k, and which line
tells the orientation), ~0.1 V for a powered cable's Ra. Source impedance 23.5 k, and 1 nF is much
larger than the ~10 pF sample-and-hold capacitor, so there is no droop; the filter sits behind the
series resistor and never loads the CC line itself.

**This is also the back-feed guard** (24 Sep 2026). R11 was changed to a pull-**down**, so EN_USB is
off until the X035 drives it high, and firmware enables the port only when a CC line reads the Rd
band. A foreign source plugged into J2 is another Rp, so both CC lines stay pulled high and read
exactly like an empty socket — the port is never enabled, and a disabled SY6280 blocks reverse
current into the +5V rail. `MEZZ_PRESENT` is a separate matter: it says a mezzanine has taken the
USB uplink, which is the *other* reason to keep J2 off. Combined rule:
**enable EN_USB only when MEZZ_PRESENT is open AND a CC line reads Rd.**

### 2.5 Protection and filtering (review 21 Sep 2026)

Added after a net-by-net review: ESD9B5.0ST5G on the host port's CC1/CC2 (D12, D13) and on
USB_VBUS at J2 (D18 — a 5 V clamp; the 6 V-max SY6280 sits behind it); 1 µF at each SY6280 IN
(C27–C29); 100 nF at the INA180 supply pin (C30); 100 nF at the two ADC divider outputs V_IN /
V_5V (C31, C32); 10 k
pull-up on MEZZ_CS to +3V3_MCU (R33) so a mezzanine SPI device stays deselected while the Pico
boots. Not added, on purpose: ESD on the mezzanine pads (internal, screwed interface) and host-side
15 k pull-downs on USB D± (Pico-PIO-USB enables the RP2040's internal ones; MOD-0008 enumerated
that way). I2C-port ESD stays on the hub: SDA/SCL are shared nets, so one pair per bus clamps a
discharge entering at any socket, hub or module — the modules themselves carry none.

**Firmware rule (from the same review):** the X035's own I2C link to the Pico is on bus A, whose
pull-ups sit on the switchable +3V3_A. A bus power-cycle must therefore always be **timed**
(e.g. 250 ms) and never "off until told" — with bus A off the Pico cannot ask for it back.

### 2.2 Regulator values (from datasheets)

- **TPS54302** (TSOT-23-6: 1 GND, 2 SW, 3 VIN, 4 FB, 5 EN, 6 BOOT). V_FB = 0.596 V.
  5 V design per datasheet: L = 10 µH (**Isat ≥ 4 A** — load 3 A + half the ripple + margin; 5x5 mm moulded), C_OUT = 2 × 22 µF 10 V, C_IN = 10 µF 50 V + 100 nF,
  C_BOOT = 100 nF, R_top = 100 k, R_bot = 13.3 k (→ 5.08 V), C_ff = 75 pF.
  **EN (added 24 Sep 2026):** to X035 **PB7** (pin 18) with **C35 = 22 nF to GND** and *no* pull-down.
  EN floating = enabled (internal ~0.7 µA pull-up), so the 22 nF gives a **~38 ms default-on delay**:
  the X035 is alive within ~2 ms (ME6203 is always-on), pulls EN low, holds the 5 V rail off through
  PD negotiation and releases it once the contract stands — and if the firmware never runs, the cap
  charges anyway and the board comes up at 5 V. The axiom holds: the X035 can *delay* and *shut down*
  the rail, it cannot prevent it. It is also the only way the X035 can kill the whole 5 V rail on
  over-current or over-temperature. A divider from VBUS_PD to EN is **not** allowed — EN's absolute
  maximum is 7 V.
  **EN_A / EN_B / EN_USB stay pulled up** (R9–R11, 100 k to +3V3_MCU): the SY6280 datasheet says its
  EN pin must not float, and X035 GPIOs are high-Z out of reset, so without the pull-ups the bus
  switches would be undefined at every boot and permanently if the firmware hung. Lower them to 10 k
  if noise pickup is ever a concern; do not remove them.
- **AP2114H-3.3** (SOT-223: 1 GND, 2 VOUT/tab, 3 VIN). 1 A, dropout 450 mV typ at 1 A. C_IN 1 µF, C_OUT 10 µF.
- **ME6203A33M3G** (SOT-23-3: 1 VSS, 2 VOUT, 3 VIN). 40 V in, 100 mA. C_IN 1 µF 50 V, C_OUT 1 µF.
- **SY6280AAC** (SOT-23-5: 1 OUT, 2 GND, 3 ISET, 4 EN, 5 IN). I_lim = 6800 / R_SET: 4.7 k → 1.45 A (USB), 10 k → 0.68 A (buses). Max 6 V.
- **INA180A2** (SOT-23-5: 1 OUT, 2 GND, 3 IN+, 4 IN−, 5 VS). Gain 50 V/V, CM −0.2…26 V.
  10 mΩ shunt: 5 A → 50 mV → 2.5 V. RC 1 k / 100 nF on OUT.

---

## 3. CH32X035G8U6 pin assignment (QFN-28) - as fabricated, rev 1.0

**Source: the pad nets of U1 in `hardware/kicad/MOD-0011.kicad_pcb`, read 17 Oct 2026, and bench-checked at
bring-up (DEV-117)**. Rewritten 18 Oct 2026: the earlier table in this section
was an older pin plan and did not match the board. Port names per pin: datasheet QFN-28 pinout diagram.

KiCad symbol `MCU_WCH_RiscV:CH32X035G8U6`, footprint
`Package_DFN_QFN:WQFN-28-1EP_4x4mm_P0.4mm_EP2.7x2.7mm`. The QFN-28 does **not** bond out PA10/PA11 (default
I2C). I2C uses the remap onto PC16/PC17 (the USB D-/D+ pins, unused here). No crystal (HSI 48 MHz). No reset
pin; program via 2-wire SWD on J8 (bottom side: GND, SWDIO, +3V3_MCU, SWCLK).

| Pin | Port | Net | Function | Board-side parts, default |
| --- | --- | --- | --- | --- |
| 1 | PC15 | CC2 | PD PHY CC2 (J1) | external 5.1 k Rd |
| 2 | VDD | +3V3_MCU | supply | ME6203, always on |
| 3 | PC0 / A10 | CC1_H_SENSE | ADC: J2 host-port CC via 47 k / 47 k + 1 nF | J2 empty: line 4.05 V; a sink pulls it to 0.9 V |
| 4 | PC3 / A13 | CC2_H_SENSE | ADC: same, other CC line | as pin 3 |
| 5 | PA0 / A0 | V_IN | ADC: input voltage, R5 100 k / R6 10 k, C31 100 nF | x11 |
| 6-8 | PA1-PA3 | - | spare | |
| 9 | PA4 / A4 | V_5V | ADC: +5V rail, R14 100 k / R15 10 k, C32 100 nF | x11 |
| 10 | PA5 / A5 | I_IN | ADC: INA180A2 on R3 10 mohm, via R4 1 k / C1 100 nF | 0.5 V per A, offset about +14 mA |
| 11-13 | PA6, PA7, PB0 | - | spare | |
| 14 | PB3 | PICO_RUN | Pico RUN, **open-drain only** | Pico's own pull-up |
| 15 | PB4 | MEZZ_EN | mezzanine 5 V enable (U11 EN) | R30 100 k pull-**down**: OFF. Drive high = on |
| 16 | PB1 / A9 | THERMAL | ADC: NTC TH1 (10 k B3950, next to U2) to GND, R37 10 k to +3V3_MCU, C38 100 nF | input/ADC only: pin 16 is bonded to PB5 inside the G8U6 |
| 17 | PB6 | EN_B | bus B power enable (U3 EN) | R10 100 k pull-up: ON. Drive low (open-drain) = off |
| 18 | PB7 | EN_BUCK | TPS54302 enable | no pull, C35 22 nF: buck self-starts. Drive low = 5 V rail off |
| 19 | PB8 | MEZZ_PRESENT | input: mezzanine grounds it | R8 100 k pull-up |
| 20 | PB9 | MEZZ_FAULT | input: mezzanine pulls low on a fault | R29 100 k pull-up |
| 21 | PB10 | EN_USB | J2 VBUS enable (U8 EN) | R11 100 k pull-**down**: OFF. Drive high = on |
| 22 | PB11 | EN_A | bus A power enable (U7 EN) | R9 100 k pull-up: ON. Drive low (open-drain) = off |
| 23 | PB12 | LED_K | status LED D4 cathode | active low |
| 24 | PC19 | SWCLK | debug | J8 |
| 25 | PC18 | SWDIO | debug | J8 |
| 26 | PC16 | SCL_A | I2C (remap) on module bus A | R24 3.3 k to +3V3_A |
| 27 | PC17 | SDA_A | I2C (remap) on module bus A; **also the BOOT-detect pin** | R23 3.3 k to +3V3_A |
| 28 | PC14 | CC1 | PD PHY CC1 (J1) | external 5.1 k Rd |
| 29 | EP | GND | ground (only ground connection) | |

Spare: pins 6, 7, 8, 11, 12, 13 (PA1, PA2, PA3, PA6, PA7, PB0). Avoid A3 and A7 for analog (not present on
some lots, datasheet note 3).

**Defaults with no firmware** (bench-verified): bus A ON, bus B ON, J2 VBUS OFF, mezzanine 5 V OFF, buck ON.
EN_A / EN_B / EN_BUCK: release (input) = on, drive low = off. EN_USB / MEZZ_EN: release = off, drive high = on.

**BOOT pin (bench result, DEV-117):** PC17 high at a software or watchdog reset does NOT trap the X035 in its
factory bootloader. At every power-up the factory bootloader runs first and starts the application with a
software reset, so a cold start reports POWER-ON + SOFTWARE in RCC_RSTSCKR: check the power-on flag first.

**Firmware note:** external 5.1 k Rd on CC1/CC2 - the X035's internal Rd must stay DISABLED.

---

## 4. Pico interface - as fabricated, rev 1.0

**Source: pad nets of J14 (socket pins 1-20) and J15 (pins 21-40) in the PCB, bench-checked in DEV-117.**
Rewritten 18 Oct 2026. Since 17 Oct 2026 the noknok I2C pins are configuration, not a fixed GPIO standard
(Ecosystem `software/readme.md`); MOD-0011 is the reference carrier.

| Function | Pico GPIO | Socket pin | Notes |
| --- | --- | --- | --- |
| PIO-USB D+ / D- | GP0 / GP1 | 1 / 2 | via R21/R22 22 ohm to J2 and mezzanine cluster D. D+ must be the lower GPIO |
| I2C bus A SDA / SCL | GP10 / GP11 | 14 / 15 | J3, J4, mezzanine cluster C, X035 |
| I2C bus B SDA / SCL | GP20 / GP21 | 26 / 27 | J5, J6 |
| Mezzanine SPI MISO / CS / SCK / MOSI | GP16 / GP17 / GP18 / GP19 | 21 / 22 / 24 / 25 | SPI0. CS: R33 100 k pull-up |
| MEZZ_IRQ | GP22 | 29 | R31 100 k pull-up |
| RUN | - | 30 | from X035 PICO_RUN (open-drain): lets the power manager reset a hung Pico |
| VSYS | - | 39 | from +5V via D5 (SS34) |

- Brain `settings.toml` for this board: `NOKNOK_I2C_SDA = "GP10"`, `NOKNOK_I2C_SCL = "GP11"`,
  `NOKNOK_USB_DP = "GP0"`, `NOKNOK_USB_DM = "GP1"`. Bus B needs two-bus support in the Conductor (DEV-74).
- GP8/GP9 and every other GPIO are not connected. VBUS (pin 40), 3V3 (36), 3V3_EN (37), ADC_VREF (35) and
  AGND (33, tied to GND on the Pico itself) are unconnected on the carrier.
- Power: the Pico runs from VSYS through D5. Its own micro-USB may be plugged in at the same time: diode-OR at
  VSYS. The Pico's USB never powers the hub rails, so the buses, J2 and the X035 need power on J1.
- Pull-ups 3.3 k per bus, **on the bus rail** (+3V3_A / +3V3_B), so a power-cycled bus takes SDA/SCL down with
  it and no module is back-fed through its I/O clamps.
- No level shifter.

## 5. I2C module ports (4 × JST-SH 4-pin)

Ports 1–2 on bus A (+3V3_A), ports 3–4 on bus B (+3V3_B). (Reduced from six to four on 21 Sep
2026 for board area; two per bus, daisy-chaining covers the rest.) Per port: 100 nF + 4.7 µF local,
ESD9B5.0ST5G on SDA/SCL. No per-port PTC — the per-bus SY6280 current limit (0.68 A) and switch replace it. Per-socket voltages, currents and switchability: §5b.

## 5b. Socket overview — what each connector carries, and what the X035 can switch

Read back from the schematic (rev 0.9) on 24 Sep 2026: current limits are the SY6280 `Rset` values
(I_lim = 6800 / Rset), enables are the X035 port pins, defaults are the pull-up/pull-down on each EN.

| Socket | Carries | Voltage | Max current | Switchable by the X035 | Default |
| --- | --- | --- | --- | --- | --- |
| **J1** USB-C "PWR" | PD input | 5 / 9 / 15 / 20 V (negotiated) | **3 A design limit** (60 W); parts rated 5 A | **No** — it is the input; but the X035 *chooses the voltage* through the PD contract | live whenever a charger is attached |
| **J2** USB-C "host" | 5 V + USB 2.0 full speed | 5 V | **1.45 A** (U8, Rset 4.7 k) | **Yes** — `EN_USB`, PB10 → U8 | **OFF** (R11 pulls down) |
| **J3, J4** Qwiic, bus A | 3.3 V + I2C | 3.3 V | **0.68 A shared** by J3 + J4 + mezzanine cluster C (U7, Rset 10 k) | **Yes** — `EN_A`, PB11 → U7 + U9 | **ON** (R9 pulls up) |
| **J5, J6** Qwiic, bus B | 3.3 V + I2C | 3.3 V | **0.68 A shared** by J5 + J6 (U3, Rset 10 k) | **Yes** — `EN_B`, PB6 → U3 + U6 | **ON** (R10 pulls up) |
| **Mezz cluster A** | raw PD power | 5–20 V | 6 A of contact rating (4 × 1.5 A); **~2.3 A in practice** — 3 A in, less the hub's own draw | **No** — hard-wired to VBUS_PD after the shunt | live with J1 |
| **Mezz clusters B + D** | switched 5 V | 5 V | **1.45 A shared** by both clusters (U11, Rset 4.7 k) | **Yes** — `MEZZ_EN`, PB4 → U11 | **OFF** (R30 pulls down) |
| **Mezz cluster C** | bus A 3.3 V + I2C | 3.3 V | shares bus A's 0.68 A | **Yes** — `EN_A`, the same switch as J3/J4 | **ON** |
| **Pico socket** (U10) | VSYS | ≈ 4.8 V (5 V − the SS34 drop) | the Pico's own regulator | **Indirectly** — the whole 5 V rail via `EN_BUCK` (PB7), or hold the Pico in reset with `PICO_RUN` (PB3) | ON ~38 ms after power-up (§2.2) |
| **J8** SWD flash pads | +3V3_MCU | 3.3 V | ~15 mA | **No** — always-on rail | ON |

Three things this table makes visible:

**The 0.68 A bus limits are trip points, not ratings.** At 0.68 A an AP2114H dissipates 1.15 W and
reaches ~140 °C junction — its own thermal shutdown. With the ~100 mm² of copper the board can
afford, the *sustainable* figure is **≈ 0.4 A per bus**; that is the number to publish. Setting
Rset to 17 k would make the electrical limit match the copper.

**The switch limits deliberately over-subscribe the 5 V rail.** The buck supplies 3 A, while the
sum of the limits is USB 1.45 + MEZZ_5V 1.45 + two LDO inputs ≈ 1.36 + Pico 0.5 = **4.8 A**. That is
intentional: no single port can pull the board down on its own, and the X035's input-current
measurement (R3 + U4 → `I_IN`) enforces the total. Firmware rule: check `I_IN` before enabling the
host port and a hungry mezzanine together.

**Only three things cannot be turned off:** the PD input itself, mezzanine cluster A (raw VBUS_PD)
and the SWD pads' 3.3 V. Everything a user can plug something into is switchable.

**Default states are decided case by case** (24 Sep 2026), not by a blanket rule:

- `EN_USB` and `MEZZ_EN` default **OFF**, because a foreign supply can appear on those connectors.
  A disabled SY6280 blocks reverse current ("at shutdown, OUT can be forced higher than IN"), so a
  stranger's charger in J2 cannot back-feed the +5V rail. Keeping VBUS off until a sink is detected
  is also what the USB-C spec requires of a source.
- `EN_A` and `EN_B` default **ON**, because only sinks connect to a Qwiic port — nothing out there
  can drive 3.3 V back into the hub. Just as important: the X035's own I2C link to the Pico rides on
  bus A, whose pull-ups sit on the switched +3V3_A. If bus A defaulted off and the firmware hung
  before enabling it, the Pico could not see the X035 at all — no diagnosis, no recovery, no I2C
  firmware update — and a board with an unflashed X035 would be dead on arrival at bring-up.

## 6. Mezzanine interface — rev 2, FROZEN 22 Sep 2026

The PicoHub's bottom carries 24 spring contacts so a **swappable mezzanine** can sit underneath it:
a DataPower hub (USB hub + ports), an amplifier/speaker board, a servo controller, a storage or
Ethernet board, a battery board — anything that wants the PicoHub's power plus a data link to the
Pico. The PicoHub side is pads only (no parts, no cost); the mezzanine carries the spring pins.
This interface is frozen: a PicoHub respin must keep it, and every mezzanine designs to it.
(Rev 1, 21 Sep, was one 2 × 12 strip; replaced before any board was made.)

**Four clusters, each a complete port on its own, each beside a mounting hole.** A mezzanine
populates only the clusters it uses; the spring force goes straight into the nearest screw.

| Cluster | Hole | Outer row (on the hole axis, toward the board edge) | Inner row (3 mm inboard) | It is |
| --- | --- | --- | --- | --- |
| **A** (2 × 4) | H1 | A5 GND · A6 GND · A7 GND · A8 GND | A1 VBUS_PD · A2 VBUS_PD · A3 VBUS_PD · A4 VBUS_PD | raw PD power, 5–20 V. **One net per row**, so each row merges into a single copper bar: 4 × 1.5 A = **6 A each way**. GND is the outer row — it drops straight onto the ground plane, and it keeps 20 V off the board edge |
| **B** (2 × 4) | H2 | B1 MEZZ_5V · B2 MEZZ_IRQ · B3 MEZZ_EN · B4 GND | B5 MEZZ_MOSI · B6 MEZZ_SCK · B7 MEZZ_CS · B8 MEZZ_MISO | an SPI port on pads. Power and rarely-used lines on the outer row, where they can be squeezed into the strip between the contacts and the board edge; the SPI group inboard where the Pico's pins are. GND at B4, next to the hole |
| **C** (2 × 2) | H5 | C1 SDA_A · C2 +3V3_A | C3 SCL_A · C4 GND | a Qwiic port on pads |
| **D** (2 × 3) | H4 | D1 MEZZ_5V · D2 MEZZ_PRESENT · D3 USB_DP | D4 GND · D5 MEZZ_FAULT · D6 USB_DN | a USB port on pads. Re-allocated 29 Sep 2026 so each function sits on one column of the 2 × 3 grid: power D1/D4, USB pair D3/D6, status D2/D5. The USB pair lands on adjacent contacts and D6/D7's ESD clamps reach it, because cluster D taps the J2 side of R21/R22 |

Tally: cluster A has VBUS_PD × 4 and GND × 4 (6 A each way; the board's design point is 3 A); three more GND (B4,
C4, D4), MEZZ_5V × 2 (one switch), +3V3_A × 1 and 12 signals = **26 contacts**.

| Net | PicoHub endpoint | Purpose |
| --- | --- | --- |
| VBUS_PD | input path, **after** the shunt R3 | raw PD voltage; the mezzanine regulates it itself. A *sourcing* mezzanine (battery board) may feed it in through its own reverse-blocking diode — see below |
| MEZZ_5V | **U11** SY6280AAC from +5V, Rset 4.7 k → 1.45 A, EN = MEZZ_EN | switched, current-limited, soft-started 5 V for small boards: a fault on the mezzanine trips the switch, not the Pico. A 3.3 V board adds a small LDO |
| +3V3_A | bus-A rail (U9 VOUT) | so an I2C-only mezzanine is literally a fourth Qwiic port on bus A; shares bus A's 0.68 A |
| MEZZ_EN | X035 pin 15 (PB4), 100 k pull-**down** (R30) → default **OFF** | one meaning for every board: high = mezzanine powered. Drives U11's EN *and* is the enable line for boards with their own buck: **a mezzanine powered from cluster A must gate its load with MEZZ_EN** (ADR-004 / D8, 18 Oct 2026: it is how the power manager sheds a mezzanine when hot); the recovery move is the same as EN_A / EN_B / EN_USB |
| MEZZ_PRESENT | X035 pin 19 (PB8), 100 k pull-up | the mezzanine grounds it to say **"I take the USB uplink"** → X035 turns EN_USB off so J2 cannot put a second device on the same D± pair. SPI/I2C-only boards leave it open; J2 stays live |
| MEZZ_FAULT | X035 pin 20 (PB9), input, pull-up | mezzanine pulls low on over-current / over-temperature; the X035 can toggle MEZZ_EN or throttle without waiting for the input shunt to notice |
| USB_DP / USB_DN | Pico GP0 / GP1 (PIO-USB, full speed) | hub uplink. Cluster D sits on the **J2 side** of the 22 Ω series resistors R21/R22, so one resistor damps both branches and D6/D7's ESD clamps protect the mezzanine contacts too |
| MEZZ_SCK / MOSI / MISO / CS | Pico GP18 / GP19 / GP16 / GP17 (SPI0) | 30–60 Mbit/s link: storage, Ethernet, a local display. CS pulled up (R33) so the mezzanine idles deselected |
| MEZZ_IRQ | Pico GP22, pull-up | "data ready / link up / card inserted" — without it the Pico polls |
| SDA_A / SCL_A | bus A (pull-ups on the PicoHub) | a smart mezzanine enumerates as a noknok I2C module |

Naming rule: `MEZZ_` prefix for lines that exist only for the mezzanine; shared buses keep their
board-wide names. PIO-USB sits on GP0/GP1 (consecutive, D+ lower — pico-pio-usb requires it), which leaves SPI0 on GP16–GP19.

**Geometry (the geometry is part of the interface).** Set in the footprint editor on
24 Sep 2026; the footprints are the master, this table is read back from them. Coordinates in mm
relative to the cluster's own **M2.5 mounting hole = the footprint origin**, in the hub pad
footprint's own frame (the footprints are authored front-side and placed on the hub **bottom**, so
KiCad mirrors them about X; the mezzanine spring-pin footprints are the exact Y mirror, generated from them by a
noknok helper script). Pitch 3.0 mm throughout; hub pads Ø 2.4 (ENIG, no paste),
mezzanine pads Ø 2.6 (with paste).

**One grid for all four clusters** (24 Sep 2026): 3.0 mm, two rows 3.0 mm apart with one row on the hole's
own axis, columns at 4.5 / 7.5 / 10.5 / 13.5 mm from the hole centre. The 4.5 mm first column clears the
M2.5 standoff (2.8 mm courtyard + 1.2 mm pad radius = 4.0 mm minimum). C and D are simply shorter versions
of the same pattern; A and B are geometrically identical.

| Cluster | Hole | Contacts (x, y) |
| --- | --- | --- |
| A | H1 | A1 (3, −4.5) · A2 (3, −7.5) · A3 (3, −10.5) · A4 (3, −13.5) · A5 (0, −4.5) · A6 (0, −7.5) · A7 (0, −10.5) · A8 (0, −13.5) |
| B | H2 | B1 (−13.5, 0) · B2 (−10.5, 0) · B3 (−7.5, 0) · B4 (−4.5, 0) · B5 (−13.5, −3) · B6 (−10.5, −3) · B7 (−7.5, −3) · B8 (−4.5, −3) |
| C | H5 | C1 (3, 4.5) · C2 (0, 4.5) · C3 (3, 7.5) · C4 (0, 7.5) |
| D | H4 | D1 (−10.5, 3) · D2 (−7.5, 3) · D3 (−4.5, 3) · D4 (−10.5, 0) · D5 (−7.5, 0) · D6 (−4.5, 0) — positions unchanged, only the D3/D4 nets swapped |

**Silkscreen key.** Both the hub pad footprints and the mezzanine spring-pin footprints carry a short
mnemonic beside every contact, so the interface reads off the board without this document. Four
characters is the limit at a 3.0 mm column pitch:

| On silk | Net | | On silk | Net |
| --- | --- | --- | --- | --- |
| `VPD` | VBUS_PD | | `SDA` / `SCL` | SDA_A / SCL_A |
| `GND` | GND | | `3V3` | +3V3_A |
| `5V` | MEZZ_5V | | `D+` / `D-` | USB_DP / USB_DN |
| `EN` / `IRQ` | MEZZ_EN / MEZZ_IRQ | | `PRSN` / `FLT` | MEZZ_PRESENT / MEZZ_FAULT |
| `MOSI` `MISO` `SCK` `CS` | the SPI group | | | |

Labels sit 2.2 mm outward from their own row. Where a cluster sits close to a board edge a label may
overhang it; move the text in the board, not in the footprint.

**Contact clearance** (24 Sep 2026): **0.2 mm** on B, C and D, so a 0.2 mm track fits between two
contacts (3.0 mm pitch − 2.4 mm pad = 0.6 mm gap = 0.2 + 0.2 + 0.2) and an escape via clears the
board edge; **0.4 mm** on cluster A, which sits at up to 20 V and needs nothing routed through it
(pad-to-pad DRC still passes: the 0.6 mm gap is larger than the 0.4 mm requirement). Clearance never
applies between items of the **same net**, which is why cluster A's one-net-per-row layout lets each
row merge into a single copper bar, and why every **power contact merges with its pour** rather than
hanging off thermal spokes: A1–A8 (both of cluster A's bars), B1 + B4, C1 + C2, D3 + D4 are set to a
*solid* zone connection. Signal contacts are left on the default.

The M2.5 hole keeps a **0.3 mm** clearance (matching the rest of the noknok library) so no pour reaches the drilled edge, and carries a
**courtyard on both sides** (Ø 5.6 mm, F.CrtYd + B.CrtYd) so nothing is placed under the screw head
or standoff on either layer. The contact field's courtyard stays single-sided on purpose: the top
side above the pads is free for parts (the Pico sits there).
**Spring pin** (mezzanine only): BAT WIRELESS BWCD-L5.5W2.0H3.0, LCSC C2826550 — SMD, Ø 2.0 × 4.0 mm
body, Ø 1.5 plunger, 5.5 mm free length, **1.5 mm stroke**, 70 ± 20 gf at working height, 1.5 A,
30 V, < 30 mΩ, 10 000 cycles. Work at 2/3 stroke: **4.5 mm board-to-board = 1.0 mm compression**,
set by **M2.5 × 4.5 mm standoffs** (5 mm leaves a third of the stroke, 4 mm bottoms the plungers
out). Full population pushes ~17 N, a B-only board ~5 N; the screws carry it, the solder never sees
tension — the joint is loaded in compression. The failure to design out is side load while mating:
**mate straight, guided** — male–female standoffs fixed to the mezzanine enter the hub's holes before
the plungers touch; thumb screws from the hub top make it tool-less.

**Rules for a mezzanine design:** **gate your load with MEZZ_EN** if you take power from cluster A (the power manager turns it off on heat, over-current or MEZZ_FAULT); start safely on anything from 5 to 20 V on cluster A (the board picks the voltage by demand; a product can set a 15/20 V minimum for a mezzanine that needs it); **limit your own inrush** — cluster A is unswitched and hands you raw VBUS, so use your regulator's soft-start or an inrush limiter; regulate from VBUS_PD (cluster A) or take ≤ 1.45 A from MEZZ_5V
(B/D), never both for the same load; +3V3_A only for I2C-class loads; assert PRESENT only if you
carry a USB device or hub; keep copper off the zone under the Pico antenna; respect the M2.5 hole
pattern (§7); a power-*sourcing* mezzanine (battery / power-bank board) feeds VBUS_PD through its own
ideal-diode or Schottky so a PD charger on J1 cannot push 20 V into it — and while it sources, J1 is
not usable (a USB-C source refuses to start on a non-zero VBUS). Rev 0.8 option if simultaneous
charging is wanted: a 5 A ideal-diode controller (LM74700-class) on the J1 path.
## 7. Mechanical

80 × 40 mm, 0.2 mm corner radius, Ø 2.8 mm M2.5 holes shared with the mezzanine standoffs. Four of
them live *inside* the cluster footprints (the footprint origin **is** the hole), one is plain, and
the sixth is the keyed hole embedded in the SWD flash-pad footprint (J8), per the noknok flashing
standard.

**Hole positions are a per-board layout choice, not part of the interface, and are deliberately not
specified here.** Place the cluster footprints wherever the routing wants them, then read the
positions and rotations back off the board and hand those to whoever designs the mating mezzanine.
What *is* frozen is the geometry inside a cluster — the pad pattern relative to its own hole, in
§6 — because that is what makes any mezzanine mate with any hub. **2-layer** (top = parts + routing, bottom = ground plane +
mezzanine pads; bottom jogs only where unavoidable, never under the buck or across the USB pair).
4-layer is the fallback if routing under the Pico socket gets ugly — same netlist, decided after
placement. All parts top side.
Keep-outs: no copper on any layer under the Pico's antenna end (last 10 mm of the Pico
footprint) — the mezzanine must respect the same zone from below. The Pico is socketed:
`noknok_RaspberryPi_Pico_Socket_THT` (2 × 1×20 female headers 2.54 mm, 8.5 mm) — its courtyard covers only the header strips, so parts may sit under the raised Pico; keep the SOT-223 LDOs and the inductor out
from under it. Stack: 1.6 board + 8.5 header + 1.0 Pico + ~3.2 Pico USB socket ≈ 14.3 mm above the
top surface; a mezzanine adds 4.5 gap + 1.6 below (§6).

## 7b. Schematic (phase 3)

`hardware/kicad/MOD-0011.kicad_sch` + two sheets (*PD input and power manager*, *Power rails*),
**rev 0.5, 21 Sep 2026** — hand-tuned in KiCad, references re-annotated gap-free (rev 0.2 was generated by a
script, since retired). KiCad 10 ERC: 0 violations with all severities enabled.
Netlist read back from KiCad and checked net-by-net against this spec (108 components).
PDF: `hardware/MOD-0011-schematic.pdf`. ESD: ESD9B5.0ST5G on the PD input CC pins, the host port CC
pins, USB D+/D−, USB_VBUS and every port's SDA/SCL (§2.5).
## 8. Bill of materials (key parts)

The complete, orderable BOM (manufacturer part numbers and LCSC codes) is `hardware/MOD-0011-bom.csv`, generated from the schematic. The table below is the design rationale for the key parts.

| Ref | Part | LCSC | Notes |
| --- | --- | --- | --- |
| U1 | CH32X035G8U6 QFN-28 | C7437027 | power manager |
| U2 | TPS54302DDCR | C311983 | 5 V buck |
| U9, U6 | AP2114H-3.3TRG1 | C150716 | bus LDOs (A, B) |
| U5 | ME6203A33M3G | C87842 | X035 supply |
| U8, U7, U3, **U11** | SY6280AAC | C55136 | load switches: USB (Rset 4.7 k), bus A, bus B (Rset 10 k), MEZZ_5V (Rset 4.7 k, EN = MEZZ_EN) — U11 pending in the schematic (rev 0.7) |
| U4 | INA180A2IDBVR | C192764 | input current sense (the three per-rail sensors were dropped, §2.3) |
| U10 | Raspberry Pi Pico 2 W (socketed) | — | brain |
| F1 | 2920L500/24GR (5 A / 24 V PTC) | C19078762 | or BSMD2920-500-24V C20539727 |
| D3 | SMAJ24A-class TVS, SMA | tbd stocked brand | check stock at order time; unidirectional, cathode to VBUS |
| D1, D2, D6–D18 | ESD9B5.0ST5G SOD-923 | (any) | PD CC ×2, host CC ×2, USB D± ×2, USB_VBUS ×1, ports SDA/SCL ×8 |
| C27–C29 | 1 µF 0603 at each SY6280 IN | (any) | §2.5 |
| C30, C31, C32 | 100 nF 0603 | (any) | INA180 VS, V_IN, V_5V |
| R33 | 10 k pull-up, MEZZ_CS → +3V3_MCU | (any) | §2.5 |
| R29, R30, R31 | 100 k pull-ups: MEZZ_FAULT, MEZZ_EN, MEZZ_IRQ → +3V3_MCU | (any) | default states |
| D5 | SS34 | (any) | VSYS feed |
| J1, J2 | HRO TYPE-C-31-M-12, 16-pin USB-C receptacle, **20 V / 5 A** (datasheet), 10 k cycles | C165948 | JLCPCB basic part; KiCad `USB_C_Receptacle_HRO_TYPE-C-31-M-12`. Decision 21 Sep: keep the 100 W path — the 3 A parts would save little |
| J3–J6 | JST SH SM04B-SRSS-TB | as modules | four ports |
| — | BWCD-L5.5W2.0H3.0 pogo pin (mezzanine side only) | C2826550 | ×24 on the mezzanine |
| D4, R7 | red LED C2286 + 2.2 k C4190 | C2286, C4190 | the standard noknok status LED, active-low on PB12 |
| L1 | Chilisin AMRU00060630100MA1, 10 µH, Isat 4 A, 7.3 × 6.8 mm moulded | C2981193 | replaces the 5 × 5 mm footprint |
| R27, R28, R35, R36 | 47 k CC-sense divider into X035 PC0/PC3, with C36/C37 1 nF at the pins | C25819 | §2.4 |
| C35 | 22 nF, TPS54302 EN delay (~38 ms default-on) | C21122 | §2.2 |

Silkscreen: `MOD-0011` + design label, per the ID scheme.
