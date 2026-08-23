#!/usr/bin/env python3
"""Render hardware/pip-pictorial.svg and .png: the Pip breadboard hookup as boxes with pins.

GPIO numbers are read from firmware/pins.hpp, so the picture cannot drift from the firmware.
Run: python3 hardware/pip-pictorial.py   (pip install schemdraw matplotlib)
"""
import re, pathlib
import schemdraw
schemdraw.use("matplotlib")
import schemdraw.elements as elm

ROOT = pathlib.Path(__file__).resolve().parent.parent
PINS = {m.group(1): int(m.group(2))
        for m in re.finditer(r"\b([A-Z][A-Z0-9_]*)\s*=\s*(\d+)", (ROOT / "firmware" / "pins.hpp").read_text())}
GPIO_TO_PIN = {0:1,1:2,2:4,3:5,4:6,5:7,6:9,7:10,8:11,9:12,10:14,11:15,12:16,13:17,14:19,15:20,
               16:21,17:22,18:24,19:25,20:26,21:27,22:29,26:31,27:32,28:34}
FIXED = {3:"GND",8:"GND",13:"GND",18:"GND",23:"GND",28:"GND",33:"GND",38:"GND",30:"RUN",
         35:"ADC_VREF",36:"3V3 OUT",37:"3V3_EN",39:"VSYS",40:"VBUS"}
GPIO_NAME = {gpio: name for name, gpio in PINS.items()}

def pico_label(p):
    if p in FIXED: return FIXED[p]
    gpio = next(g for g, pp in GPIO_TO_PIN.items() if pp == p)
    return f"GP{gpio} {GPIO_NAME[gpio]}" if gpio in GPIO_NAME else f"GP{gpio}"

def P(name):            # physical Pico pin of a pins.hpp constant
    return GPIO_TO_PIN[PINS[name]]

# Wire colours follow hardware/pip-wiring.yml
RD, BK, YE, OG, GN, BU, VT, WH, GY = "#d62728", "#111111", "#e0b400", "#ff7f0e", "#2ca02c", "#1f77b4", "#9467bd", "#9a9a9a", "#666666"

d = schemdraw.Drawing()
d.config(fontsize=9, lw=1.4)

# Pico 2 W seen from the top, USB at the top: pins 1..20 down the left, 40..21 down the right.
# schemdraw lays side pins out bottom to top, so the lists are given bottom to top.
pico_pins = [elm.IcPin(name=pico_label(p), pin=str(p), side="left", anchorname=f"p{p}") for p in range(20, 0, -1)]
pico_pins += [elm.IcPin(name=pico_label(p), pin=str(p), side="right", anchorname=f"p{p}") for p in range(21, 41)]
pico = d.add(elm.Ic(pins=pico_pins, size=(7, 14.6), pinspacing=0.7, edgepadW=0.4, edgepadH=0.6, plblsize=7)
             .label("Raspberry Pi Pico 2 W, top view, USB at the top", loc="top", ofst=0.5, fontsize=11)
             .at((0, 0)).anchor("center"))
d.add(elm.Dot(radius=0.16).at(pico.p1).color("black"))
d.add(elm.Label().at(pico.p1).label("pin 1", loc="left", ofst=0.45, fontsize=8))
d.add(elm.Label().at((0, 7.3)).label("[ USB ]", fontsize=8))

def ic(name, pins, side, center, title, w=4.2, extra=()):
    """Breakout as a box; pins listed top to bottom as printed on the board."""
    h = 0.7 * max(len(pins), len(extra)) + 1.0
    ps = [elm.IcPin(name=n, pin=str(i + 1), side=side, anchorname=re.sub(r"\W", "_", n)) for i, n in enumerate(pins)]
    ps = ps[::-1]                          # bottom to top for schemdraw
    other = "right" if side == "left" else "left"
    ps += [elm.IcPin(name=n, side=other, anchorname=a) for n, a in extra[::-1]]
    return d.add(elm.Ic(pins=ps, size=(w, h), pinspacing=0.7, edgepadH=0.5, plblsize=7).right()
                 .label(title, loc="top", ofst=0.4, fontsize=10).at(center).anchor("center"))

def route(src, dst, xv, color, dot=False):
    """Horizontal to x=xv, vertical to the target row, horizontal to the target."""
    sx, sy = src; dx, dy = dst
    d.add(elm.Line().at((sx, sy)).to((xv, sy)).color(color))
    d.add(elm.Line().at((xv, sy)).to((xv, dy)).color(color))
    d.add(elm.Line().at((xv, dy)).to((dx, dy)).color(color))
    if dot: d.add(elm.Dot(radius=0.1).at((sx, sy)).color(color))

# Right side: amp on top (I2S pins are high on the Pico), face below (SPI pins are low)
amp = ic("amp", ["LRC", "BCLK", "DIN", "GAIN", "SD", "GND", "VIN"], "left", (16, 3.2), "MAX98357A I2S amp\nchirps, next plan",
         extra=[("SPK+", "SPKP"), ("SPK-", "SPKM")])
spk = d.add(elm.Speaker().right().at((amp.SPKP[0] + 1.8, amp.SPKP[1])).anchor("in1").label("4 ohm 3 W", loc="bottom", ofst=0.4, fontsize=8))
d.add(elm.Line().at(amp.SPKP).to(spk.in1).color(RD))
route(amp.SPKM, spk.in2, amp.SPKP[0] + 0.9, BK)
lcd = ic("lcd", ["VCC", "GND", "CS", "RESET", "DC", "SDI (MOSI)", "SCK", "LED", "SDO (MISO)"], "left", (16, -4.6), "ILI9341 240x320 SPI\nthe face")

route(pico.p32, amp.LRC, 8.4, VT)
route(pico.p31, amp.BCLK, 8.8, BU)
route(pico.p34, amp.DIN, 9.2, WH)
route(pico.p33, amp.GND, 9.6, BK)
route(pico.p36, amp.VIN, 10.0, RD, dot=True)
route(pico[f"p{P('LCD_CS')}"], lcd.CS, 8.4, YE)
route(pico[f"p{P('LCD_RST')}"], lcd.RESET, 8.8, OG)
route(pico[f"p{P('LCD_DC')}"], lcd.DC, 9.2, GN)
route(pico[f"p{P('SPI_MOSI')}"], lcd.SDI__MOSI_, 9.6, BU)
route(pico[f"p{P('SPI_SCK')}"], lcd.SCK, 10.0, VT)
route(pico.p36, lcd.VCC, 10.4, RD)
route(pico.p38, lcd.GND, 10.8, BK)
route(pico.p36, lcd.LED, 11.2, WH)

# Left side: light sensor on top (I2C pins are high), LED rows mid, button at the bottom
als = ic("als", ["VIN", "3Vo", "GND", "SCL", "SDA"], "right", (-16, 1.6), "VEML7700 light\nI2C 0x10")
route(pico.p3, als.GND, -8.4, BK)
route(pico[f"p{P('I2C_SDA')}"], als.SDA, -8.8, GN)
route(pico[f"p{P('I2C_SCL')}"], als.SCL, -9.2, YE)
# 3V3 for the sensor comes from the right side of the Pico, over the top of the board
top_y = 8.2
d.add(elm.Line().at(pico.p36).to((11.6, pico.p36[1])).color(RD))
d.add(elm.Line().at((11.6, pico.p36[1])).to((11.6, top_y)).color(RD))
d.add(elm.Line().at((11.6, top_y)).to((-9.6, top_y)).color(RD))
d.add(elm.Line().at((-9.6, top_y)).to((-9.6, als.VIN[1])).color(RD))
d.add(elm.Line().at((-9.6, als.VIN[1])).to(als.VIN).color(RD))

# RGB LED: GP10/11/12 each through 220 ohm to R, G, B; common cathode to GND pin 13
bar_x = -14.6
for name, col in (("LED_R", RD), ("LED_G", GN), ("LED_B", BU)):
    pin = pico[f"p{P(name)}"]
    d.add(elm.Line().at(pin).to((-8.6, pin[1])).color(col))
    r = d.add(elm.Resistor().at((-8.6, pin[1])).left().scale(0.6).color("black"))
    l = d.add(elm.LED().at(r.end).left().scale(0.6).color(col))
    d.add(elm.Line().at(l.end).to((bar_x, pin[1])).color(BK))
y_top, y_bot = pico[f"p{P('LED_R')}"][1], pico[f"p{P('LED_B')}"][1]
d.add(elm.Line().at((bar_x, y_top)).to((bar_x, y_bot)).color(BK))
d.add(elm.Line().at(pico.p13).to((bar_x, pico.p13[1])).color(BK))
d.add(elm.Line().at((bar_x, pico.p13[1])).to((bar_x, y_top)).color(BK))
d.add(elm.Label().at((-12.0, y_bot - 0.9)).label("RGB LED, 4 legs: R, G, B through 220 ohm; COM (longest leg) to GND", fontsize=8))

# Button: GP15 to GND pin 18, internal pull-up
pb, pg = pico[f"p{P('BUTTON')}"], pico.p18
btn = d.add(elm.Button().at((-9.0, pb[1])).left().scale(0.8).label("tactile button", loc="bottom", ofst=0.5, fontsize=8))
d.add(elm.Line().at(pb).to(btn.start).color(GY))
d.add(elm.Line().at(btn.end).to((-13.5, pb[1])).color(BK))
d.add(elm.Line().at(pg).to((-13.5, pg[1])).color(BK))
d.add(elm.Line().at((-13.5, pg[1])).to((-13.5, pb[1])).color(BK))

# Pi Zero 2 W: its 40-pin header drawn like the Pico's, two columns of 20. Hold the Zero (seen
# from above, header side up) turned so the header is vertical along the RIGHT edge of the board
# with the SD card slot at the TOP. Pin 1 is then the square pad at the top-left of the header
# (the inner row, at the SD-card end); odd pins run down the left column, even pins down the
# right column. Placed so pins 10, 8, 6 sit level with Pico pins 1, 2, 3 and the three link wires
# are straight lines.
ZERO_EVEN = ["5V","5V","GND","GPIO14 TXD","GPIO15 RXD","GPIO18","GND","GPIO23","GPIO24","GND",
             "GPIO25","GPIO8 CE0","GPIO7 CE1","ID_SC","GND","GPIO12","GND","GPIO16","GPIO20","GPIO21"]
ZERO_ODD  = ["3V3","GPIO2 SDA","GPIO3 SCL","GPIO4","GND","GPIO17","GPIO27","GPIO22","3V3","GPIO10 MOSI",
             "GPIO9 MISO","GPIO11 SCLK","GND","ID_SD","GPIO5","GPIO6","GPIO13","GPIO19","GPIO26","GND"]
# IcPin lists run bottom to top, so pin 39 is first on the left and pin 1 last (top).
zero_pins = [elm.IcPin(name=ZERO_ODD[k], pin=str(2*k+1), side="left", anchorname=f"z{2*k+1}") for k in range(19, -1, -1)]
zero_pins += [elm.IcPin(name=ZERO_EVEN[k], pin=str(2*k+2), side="right", anchorname=f"z{2*k+2}") for k in range(19, -1, -1)]
# Pico pin 1 is at y = +6.65 (top of its left column). Zero pin 10 is the 5th even pin from the
# top, at y = center + 6.65 - 4*0.7 = 6.65, so center = 2.8.
ZC = (-25.0, 2.8)
zero = d.add(elm.Ic(pins=zero_pins, size=(7, 14.6), pinspacing=0.7, edgepadW=0.4, edgepadH=0.6, plblsize=7).right()
             .label("Raspberry Pi Zero 2 W, 40-pin header, seen from above (header side up)\nturned so the header runs down the RIGHT edge of the board and the SD card slot is at the TOP\npin 1 = square pad, top-left; odd pins down the left column, even pins down the right column", loc="top", ofst=0.5, fontsize=10)
             .at(ZC).anchor("center"))
d.add(elm.Dot(radius=0.16).at(zero.z1).color("black"))
d.add(elm.Label().at((zero.z1[0] - 0.9, zero.z1[1] + 0.15)).label("pin 1 (square pad)", loc="top", ofst=0.1, fontsize=8))
d.add(elm.Label().at((ZC[0], ZC[1] - 7.9)).label("mini HDMI and both micro USB ports are on the long edge opposite the header (left side here)", fontsize=8))
# GP0 TX is level with Zero pin 10: straight. GP1 and GND step up to pins 8 and 6.
d.add(elm.Line().at(pico.p1).to(zero.z10).color(OG))
for pico_pin, zpin, col, xv in ((pico.p2, zero.z8, YE, -12.6), (pico.p3, zero.z6, BK, -12.0)):
    d.add(elm.Line().at(pico_pin).tox(xv).color(col))
    d.add(elm.Line().toy(zpin[1]).color(col))
    d.add(elm.Line().to(zpin).color(col))
d.add(elm.Dot(radius=0.1).at(pico.p3).color(BK))
for zpin, col in ((zero.z10, OG), (zero.z8, YE), (zero.z6, BK)):
    d.add(elm.Dot(radius=0.14).at(zpin).color(col))
d.add(elm.Label().at((-11.0, 8.9)).label("link: GP0 TX -> Zero 10 RXD (orange), GP1 RX <- Zero 8 TXD (yellow), GND -> Zero 6 (black); 3.3 V both sides", fontsize=7.5))

d.add(elm.Label().at((0, -9.0)).label("Wires that cross without a dot are not connected. Colours match hardware/pip-wiring.yml. Backlight LED pin straight to 3V3; Zero-Pico UART is TX to RX crossed; ILI9341 SDO, the touch row (T_CLK T_CS T_DIN T_DO T_IRQ) and amp GAIN/SD stay open: v0 has no touch, the button is the only input.\n"
                                        "Generated by hardware/pip-pictorial.py from firmware/pins.hpp.", fontsize=8))

out = ROOT / "hardware" / "pip-pictorial"
d.save(str(out) + ".svg")
d.save(str(out) + ".png", dpi=150)
print("wrote", out.with_suffix(".svg"), out.with_suffix(".png"))
