"""Ortam Aurora breadboard wiring diagram (Tinkercad/Fritzing style).

Run:  python draw_wiring.py   -> writes wiring.svg next to this file.
Pin map comes from firmware v7: I2C SDA=GPIO40, SCL=GPIO41, DHT22=GPIO42,
MAX4466=GPIO1, WS2812B data=GPIO47 (through a level shifter).
"""
from pathlib import Path

P = 20                      # breadboard pitch (0.1") in px
BX, BY = 400, 300           # breadboard origin
W, H = 1840, 1330           # canvas

ROW = {"T+": BY + 30, "T-": BY + 50,
       **{r: BY + 100 + i * P for i, r in enumerate("abcde")},
       **{r: BY + 240 + i * P for i, r in enumerate("fghij")},
       "B+": BY + 370, "B-": BY + 390}


def cx(col):
    return BX + 40 + (col - 1) * P


def hole(col, row):
    return (cx(col), ROW[row])


# ---------------------------------------------------------------- ESP32
EX, EY = 60, 240
J1 = ["3V3", "3V3", "RST", "4", "5", "6", "7", "15", "16", "17", "18",
      "8", "3", "46", "9", "10", "11", "12", "13", "14", "5V", "G"]
J3 = ["G", "TX", "RX", "1", "2", "42", "41", "40", "39", "38", "37",
      "36", "35", "0", "45", "48", "47", "21", "20", "19", "G", "G"]
J1X, J3X = EX + 12, EX + 192


def esp_pin(header, num):
    return (J1X if header == "J1" else J3X, EY + 50 + (num - 1) * P)


out = []
add = out.append

COL = {"3v3": "#e53935", "5v": "#fb8c00", "gnd": "#263238", "sda": "#1e88e5",
       "scl": "#fdd835", "dht": "#43a047", "mic": "#8e24aa", "led": "#00acc1"}
DARK = {"3v3": "#8e1b18", "5v": "#a35a00", "gnd": "#000000", "sda": "#0d47a1",
        "scl": "#a68f00", "dht": "#1b5e20", "mic": "#4a148c", "led": "#006064"}


def text(x, y, s, size=12, fill="#263238", anchor="start", weight=400, rot=None, family=None):
    tr = f' transform="rotate({rot} {x} {y})"' if rot is not None else ""
    fam = f' font-family="{family}"' if family else ""
    add(f'<text x="{x}" y="{y}" font-size="{size}" fill="{fill}" text-anchor="{anchor}" '
        f'font-weight="{weight}"{tr}{fam}>{s}</text>')


# ---------------------------------------------------------------- breadboard
def breadboard():
    w, h = 40 + 62 * P + 40, 420
    add(f'<rect x="{BX}" y="{BY}" width="{w}" height="{h}" rx="10" fill="#f1f1ec" stroke="#c9c9c0" stroke-width="2"/>')
    add(f'<rect x="{BX}" y="{BY + 195}" width="{w}" height="30" fill="#e2e2da"/>')  # centre channel
    for y, c in ((BY + 16, "#e53935"), (BY + 64, "#1e88e5"), (BY + 356, "#e53935"), (BY + 404, "#1e88e5")):
        add(f'<line x1="{BX + 30}" y1="{y}" x2="{BX + w - 30}" y2="{y}" stroke="{c}" stroke-width="2"/>')
    for (y, s, c) in ((ROW["T+"], "+", "#e53935"), (ROW["T-"], "−", "#1e88e5"),
                      (ROW["B+"], "+", "#e53935"), (ROW["B-"], "−", "#1e88e5")):
        text(BX + 14, y + 5, s, 16, c, "middle", 700)
        text(BX + w - 14, y + 5, s, 16, c, "middle", 700)
    for r, y in ROW.items():
        for col in range(1, 64):
            x = cx(col)
            add(f'<rect x="{x - 3.5}" y="{y - 3.5}" width="7" height="7" rx="1.5" fill="#55554f"/>')
        if len(r) == 1:
            text(BX + 18, y + 4, r, 11, "#8a8a80", "middle")
            text(BX + w - 18, y + 4, r, 11, "#8a8a80", "middle")
    for col in [1] + list(range(5, 64, 5)):
        text(cx(col), BY + 84, col, 10, "#8a8a80", "middle")


# ---------------------------------------------------------------- ESP32 board
def esp32():
    add(f'<rect x="{EX}" y="{EY}" width="204" height="520" rx="8" fill="#1d1d1f" stroke="#000" stroke-width="2"/>')
    # WROOM module + PCB antenna
    add(f'<rect x="{EX + 50}" y="{EY + 14}" width="104" height="250" rx="4" fill="#2b2b2e"/>')
    add(f'<rect x="{EX + 54}" y="{EY + 62}" width="96" height="196" rx="3" fill="#c7cbd1" stroke="#9aa0a6"/>')
    add(f'<polyline points="{EX + 62},{EY + 50} {EX + 62},{EY + 22} {EX + 78},{EY + 22} {EX + 78},{EY + 46} '
        f'{EX + 94},{EY + 46} {EX + 94},{EY + 22} {EX + 110},{EY + 22} {EX + 110},{EY + 46} {EX + 126},{EY + 46} '
        f'{EX + 126},{EY + 22} {EX + 142},{EY + 22}" fill="none" stroke="#c9a227" stroke-width="3"/>')
    text(EX + 102, EY + 150, "ESP32-S3", 15, "#37474f", "middle", 700)
    text(EX + 102, EY + 168, "WROOM-1", 11, "#37474f", "middle")
    text(EX + 102, EY + 300, "ESP32-S3-DevKitC-1", 11, "#e0e0e0", "middle", 600)
    # buttons, LED, USB-C
    for bx, lab in ((EX + 70, "BOOT"), (EX + 134, "RST")):
        add(f'<rect x="{bx - 12}" y="{EY + 430}" width="24" height="18" rx="3" fill="#bdbdbd"/>'
            f'<circle cx="{bx}" cy="{EY + 439}" r="5" fill="#424242"/>')
        text(bx, EY + 462, lab, 9, "#e0e0e0", "middle")
    add(f'<rect x="{EX + 96}" y="{EY + 400}" width="12" height="12" rx="2" fill="#fafafa"/>')
    text(EX + 102, EY + 394, "RGB", 8, "#bdbdbd", "middle")
    for ux, lab in ((EX + 62, "UART"), (EX + 142, "USB")):
        add(f'<rect x="{ux - 18}" y="{EY + 506}" width="36" height="20" rx="7" fill="#9e9e9e" stroke="#616161"/>'
            f'<rect x="{ux - 11}" y="{EY + 512}" width="22" height="8" rx="3" fill="#424242"/>')
        text(ux, EY + 498, lab, 9, "#e0e0e0", "middle")
    # headers
    used = {("J1", 1), ("J3", 1), ("J3", 4), ("J3", 6), ("J3", 7), ("J3", 8), ("J3", 17)}
    for hdr, names, x in (("J1", J1, J1X), ("J3", J3, J3X)):
        add(f'<rect x="{x - 8}" y="{EY + 40}" width="16" height="{22 * P}" rx="2" fill="#111"/>')
        for i, n in enumerate(names, 1):
            _, y = esp_pin(hdr, i)
            hi = (hdr, i) in used
            add(f'<rect x="{x - 4}" y="{y - 4}" width="8" height="8" fill="{"#ffd54f" if hi else "#c9a227"}"/>')
            lab = n if n in ("3V3", "5V", "G", "RST", "TX", "RX") else n
            if hdr == "J1":
                text(x + 12, y + 4, lab, 10, "#ffffff" if hi else "#cfd8dc", "start", 700 if hi else 400)
            else:
                text(x - 12, y + 4, lab, 10, "#ffffff" if hi else "#cfd8dc", "end", 700 if hi else 400)
    text(EX + 102, EY - 12, "ESP32-S3 DevKitC-1", 14, "#263238", "middle", 700)


# ---------------------------------------------------------------- modules
def header_pins(cols, labels, label_y, label_fill="#ffffff"):
    for col, lab in zip(cols, labels):
        x, y = hole(col, "a")
        add(f'<rect x="{x - 5}" y="{y - 16}" width="10" height="10" fill="#111"/>')
        add(f'<rect x="{x - 3}" y="{y - 10}" width="6" height="13" fill="#d4af37"/>')
        text(x + 4, label_y, lab, 10, label_fill, "start", 700, rot=-90)


def module_body(cols, w, h, fill, stroke="#00000055"):
    xc = (cx(cols[0]) + cx(cols[-1])) / 2
    x0, y1 = xc - w / 2, ROW["a"] - 14
    add(f'<rect x="{x0}" y="{y1 - h}" width="{w}" height="{h}" rx="6" fill="{fill}" stroke="{stroke}" stroke-width="2"/>')
    for hx, hy in ((x0 + 9, y1 - h + 9), (x0 + w - 9, y1 - h + 9)):
        add(f'<circle cx="{hx}" cy="{hy}" r="4" fill="#f1f1ec" stroke="#c9a227" stroke-width="1.5"/>')
    return xc, x0, y1 - h, y1


def oled():
    cols = [5, 6, 7, 8]
    xc, x0, top, bot = module_body(cols, 220, 196, "#14307a")
    add(f'<rect x="{x0 + 14}" y="{top + 22}" width="192" height="112" rx="4" fill="#0b0b0f" stroke="#5a6270" stroke-width="3"/>')
    for i, s in enumerate(["23.4°C   45%", "Lux 120  eCO2 410", "06:30  alarm on"]):
        text(xc, top + 60 + i * 26, s, 15, "#7fe3ff", "middle", 600, family="Consolas, monospace")
    header_pins(cols, ["GND", "VCC", "SCL", "SDA"], bot - 8)
    text(xc, top - 10, "SSD1306 OLED 0.96″ (I²C 0x3C)", 13, "#263238", "middle", 700)


def bh1750():
    cols = [16, 17, 18, 19, 20]
    xc, x0, top, bot = module_body(cols, 140, 150, "#1565c0")
    add(f'<rect x="{xc - 14}" y="{top + 36}" width="28" height="22" rx="2" fill="#111"/>')
    add(f'<rect x="{xc - 6}" y="{top + 42}" width="12" height="10" fill="#3a3a3a"/>')
    text(xc, top + 80, "GY-302", 11, "#e3f2fd", "middle", 700)
    header_pins(cols, ["VCC", "GND", "SCL", "SDA", "ADDR"], bot - 8)
    text(xc, top - 10, "BH1750 light (0x23)", 13, "#263238", "middle", 700)


def sgp30():
    cols = [25, 26, 27, 28]
    xc, x0, top, bot = module_body(cols, 130, 130, "#6a1b9a")
    add(f'<rect x="{xc - 13}" y="{top + 28}" width="26" height="26" rx="3" fill="#cfd8dc" stroke="#90a4ae"/>')
    add(f'<circle cx="{xc}" cy="{top + 41}" r="5" fill="#455a64"/>')
    text(xc, top + 74, "SGP30", 11, "#f3e5f5", "middle", 700)
    header_pins(cols, ["VIN", "GND", "SCL", "SDA"], bot - 8)
    text(xc, top - 10, "SGP30 air quality (0x58)", 13, "#263238", "middle", 700)


def dht22():
    cols = [34, 35, 36]
    xc, x0, top, bot = module_body(cols, 120, 200, "#0d47a1")
    add(f'<rect x="{xc - 42}" y="{top + 18}" width="84" height="118" rx="5" fill="#f5f5f5" stroke="#bdbdbd" stroke-width="2"/>')
    for i in range(7):
        add(f'<rect x="{xc - 30}" y="{top + 30 + i * 14}" width="60" height="6" rx="3" fill="#cfcfcf"/>')
    text(xc, top + 150, "AM2302", 10, "#e3f2fd", "middle", 700)
    header_pins(cols, ["+", "OUT", "−"], bot - 8)
    text(xc, top - 10, "DHT22 temp/humidity", 13, "#263238", "middle", 700)


def max4466():
    cols = [42, 43, 44]
    xc, x0, top, bot = module_body(cols, 110, 150, "#4527a0")
    add(f'<circle cx="{xc}" cy="{top + 48}" r="26" fill="#9e9e9e" stroke="#616161" stroke-width="3"/>')
    add(f'<circle cx="{xc}" cy="{top + 48}" r="17" fill="#212121"/>')
    add(f'<rect x="{xc + 24}" y="{top + 82}" width="18" height="18" rx="2" fill="#1e88e5"/>'
        f'<line x1="{xc + 27}" y1="{top + 91}" x2="{xc + 39}" y2="{top + 91}" stroke="#fff" stroke-width="2"/>')
    text(xc - 8, top + 96, "GAIN", 8, "#d1c4e9", "middle")
    header_pins(cols, ["OUT", "GND", "VCC"], bot - 8)
    text(xc, top - 10, "MAX4466 microphone", 13, "#263238", "middle", 700)


SHIFT_TOP = ["HV4", "HV3", "GND", "HV", "HV2", "HV1"]   # row e, cols 52..57
SHIFT_BOT = ["LV4", "LV3", "GND", "LV", "LV2", "LV1"]   # row f, cols 52..57


def shifter():
    cols = list(range(52, 58))
    x0, x1 = cx(52) - 14, cx(57) + 14
    add(f'<rect x="{x0}" y="{ROW["e"] - 14}" width="{x1 - x0}" height="{ROW["f"] - ROW["e"] + 28}" rx="5" '
        f'fill="#c62828" stroke="#00000055" stroke-width="2"/>')
    for i in range(4):
        add(f'<rect x="{x0 + 16 + i * 26}" y="{ROW["e"] + 22}" width="14" height="10" rx="1" fill="#212121"/>')
    for col, lab in zip(cols, SHIFT_TOP):
        x, y = hole(col, "e")
        add(f'<rect x="{x - 3}" y="{y - 3}" width="6" height="6" fill="#d4af37"/>')
        text(x, y + 16, lab, 8, "#ffffff", "middle", 700)
    for col, lab in zip(cols, SHIFT_BOT):
        x, y = hole(col, "f")
        add(f'<rect x="{x - 3}" y="{y - 3}" width="6" height="6" fill="#d4af37"/>')
        text(x, y - 8, lab, 8, "#ffffff", "middle", 700)
    text(1650, ROW["e"] + 24, "level shifter", 12, "#263238", "middle", 700)
    text(1650, ROW["e"] + 38, "3.3 V ↔ 5 V", 11, "#263238", "middle")


def resistor():
    (x0, y), (x1, _) = hole(57, "c"), hole(61, "c")
    add(f'<line x1="{x0}" y1="{y}" x2="{x1}" y2="{y}" stroke="#9e9e9e" stroke-width="3"/>')
    add(f'<rect x="{x0 + 18}" y="{y - 9}" width="{x1 - x0 - 36}" height="18" rx="8" fill="#e6cfa3" stroke="#8d6e63"/>')
    for i, c in enumerate(["#ef6c00", "#ef6c00", "#6d4c41", "#c9a227"]):
        add(f'<rect x="{x0 + 26 + i * 8}" y="{y - 9}" width="4" height="18" fill="{c}"/>')
    for x in (x0, x1):
        add(f'<circle cx="{x}" cy="{y}" r="3.5" fill="#757575"/>')
    text((x0 + x1) / 2, y - 14, "330 Ω", 11, "#5d4037", "middle", 700)
    badges.append((27, (x1 + 22, y - 16), "gnd"))


def capacitor():
    (xp, yp), (xn, yn) = hole(48, "T+"), hole(49, "T-")
    bx, by = (xp + xn) / 2, BY - 70
    add(f'<line x1="{bx - 6}" y1="{by + 52}" x2="{xp}" y2="{yp}" stroke="#9e9e9e" stroke-width="3"/>')
    add(f'<line x1="{bx + 6}" y1="{by + 52}" x2="{xn}" y2="{yn}" stroke="#9e9e9e" stroke-width="3"/>')
    add(f'<rect x="{bx - 16}" y="{by}" width="32" height="54" rx="6" fill="#1a237e" stroke="#000" stroke-width="1.5"/>')
    add(f'<rect x="{bx + 6}" y="{by}" width="8" height="54" fill="#9fa8da"/>')
    text(bx + 10, by + 30, "−", 12, "#1a237e", "middle", 700)
    text(bx + 24, by + 24, "1000 µF", 11, "#1a237e", "start", 700)
    text(bx + 24, by + 38, "+ leg → 5 V rail", 10, "#546e7a", "start")
    badges.append((31, (bx - 30, by + 26), "5v"))


def strip():
    x1, y0 = 1560, 108          # input end (right) at x1
    add(f'<rect x="1110" y="{y0}" width="{x1 - 1110}" height="52" rx="4" fill="#f7f7f7" stroke="#bdbdbd" stroke-width="2"/>')
    for i in range(8):
        lx = x1 - 70 - i * 44
        add(f'<rect x="{lx - 12}" y="{y0 + 14}" width="24" height="24" rx="3" fill="#fff" stroke="#9e9e9e"/>'
            f'<circle cx="{lx}" cy="{y0 + 26}" r="7" fill="#ffd54f"/>')
    text(1118, y0 + 31, "… ×60", 12, "#616161", "start", 700)
    for i, lab in enumerate(["+5V", "DIN", "GND"]):
        add(f'<rect x="{x1 - 36}" y="{y0 + 6 + i * 15}" width="30" height="11" rx="2" fill="#d4af37"/>')
        text(x1 - 40, y0 + 15 + i * 15, lab, 9, "#424242", "end", 700)
    text(1355, y0 - 10, "WS2812B LED strip (60 LEDs)  ← data flows this way", 13, "#263238", "middle", 700)


def psu():
    add('<rect x="1690" y="70" width="110" height="64" rx="8" fill="#212121"/>')
    text(1745, 98, "5 V ⎓ 4 A", 13, "#ffffff", "middle", 700)
    text(1745, 116, "adapter", 10, "#bdbdbd", "middle")
    add('<path d="M1745 134 C1745 170 1760 180 1760 205" fill="none" stroke="#111" stroke-width="5"/>')
    add('<rect x="1730" y="205" width="60" height="34" rx="4" fill="#2e7d32"/>')
    add('<rect x="1736" y="211" width="20" height="22" rx="2" fill="#a5d6a7"/><rect x="1764" y="211" width="20" height="22" rx="2" fill="#a5d6a7"/>')
    text(1746, 228, "+", 13, "#b71c1c", "middle", 700)
    text(1774, 228, "−", 13, "#000", "middle", 700)
    text(1794, 226, "DC jack", 10, "#455a64", "start")


# ---------------------------------------------------------------- wires
def rounded(points, r=8):
    d = f"M{points[0][0]},{points[0][1]}"
    for i in range(1, len(points) - 1):
        (x0, y0), (x1, y1), (x2, y2) = points[i - 1], points[i], points[i + 1]
        def towards(ax, ay, bx, by, dist):
            L = ((bx - ax) ** 2 + (by - ay) ** 2) ** 0.5 or 1
            k = min(dist, L / 2) / L
            return ax + (bx - ax) * k, ay + (by - ay) * k
        a = towards(x1, y1, x0, y0, r)
        b = towards(x1, y1, x2, y2, r)
        d += f" L{a[0]:.1f},{a[1]:.1f} Q{x1},{y1} {b[0]:.1f},{b[1]:.1f}"
    d += f" L{points[-1][0]},{points[-1][1]}"
    return d


def arc(p0, p1, bulge):
    mx, my = (p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2 + bulge
    return f"M{p0[0]},{p0[1]} Q{mx},{my} {p1[0]},{p1[1]}"


badges = []


def wire(kind, num, d, ends, badge_at):
    add(f'<path d="{d}" fill="none" stroke="{DARK[kind]}" stroke-width="7" stroke-linecap="round" stroke-linejoin="round"/>')
    add(f'<path d="{d}" fill="none" stroke="{COL[kind]}" stroke-width="4.5" stroke-linecap="round" stroke-linejoin="round"/>')
    for x, y in ends:
        add(f'<rect x="{x - 4.5}" y="{y - 4.5}" width="9" height="9" rx="1.5" fill="{DARK[kind]}"/>'
            f'<rect x="{x - 2}" y="{y - 2}" width="4" height="4" fill="#d4af37"/>')
    badges.append((num, badge_at, kind))


def wires():
    # 1-3 power
    p = esp_pin("J1", 1)
    pts = [p, (30, p[1]), (30, 790), (300, 790), (300, ROW["B+"]), hole(1, "B+")]
    wire("3v3", 1, rounded(pts), [p, hole(1, "B+")], (30, 560))
    p = esp_pin("J3", 1)
    pts = [p, (390, p[1]), (390, ROW["B-"]), hole(1, "B-")]
    wire("gnd", 2, rounded(pts), [p, hole(1, "B-")], (390, 330))
    a, b = hole(50, "T-"), hole(50, "B-")
    wire("gnd", 3, f"M{a[0]},{a[1]} L{b[0]},{b[1]}", [a, b], (a[0], ROW["c"]))

    # ESP signals through the centre channel (lanes)
    def esp_signal(kind, num, pin, bend_x, lane_y, target, badge):
        p = esp_pin("J3", pin)
        pts = [p, (bend_x, p[1]), (bend_x, lane_y), (target[0], lane_y), target]
        wire(kind, num, rounded(pts), [p, target], badge)

    esp_signal("scl", 6, 7, 300, 519, hole(7, "e"), (300, 470))
    esp_signal("sda", 7, 8, 280, 528, hole(8, "e"), (280, 490))
    esp_signal("dht", 18, 6, 320, 510, hole(35, "e"), (1060, 510))
    esp_signal("mic", 21, 4, 340, 501, hole(42, "e"), (1200, 501))
    p = esp_pin("J3", 17)
    pts = [p, (370, p[1]), (370, 642), (cx(57), 642), hole(57, "j")]
    wire("led", 24, rounded(pts), [p, hole(57, "j")], (1180, 642))

    # module power: VCC -> bottom + (3.3 V), GND -> bottom −
    num = 4
    for vcc, gnd in ((6, 5), (16, 17), (25, 26), (34, 36), (44, 43)):
        a = hole(vcc, "e"); b = hole(vcc, "B+")
        wire("3v3", num, f"M{a[0]},{a[1]} L{b[0]},{b[1]}", [a, b], (a[0], 590))
        a = hole(gnd, "e"); b = hole(gnd, "B-")
        wire("gnd", num + 1, f"M{a[0]},{a[1]} L{b[0]},{b[1]}", [a, b], (a[0], 560))
        num += 4 if vcc in (6, 16, 25) else 3
    # I2C chain
    wire("scl", 10, arc(hole(7, "d"), hole(18, "d"), -22), [hole(7, "d"), hole(18, "d")], (cx(12) + 10, 449))
    wire("sda", 11, arc(hole(8, "c"), hole(19, "c"), -22), [hole(8, "c"), hole(19, "c")], (cx(13) + 10, 429))
    wire("scl", 14, arc(hole(18, "c"), hole(27, "c"), -22), [hole(18, "c"), hole(27, "c")], (cx(22) + 10, 429))
    wire("sda", 15, arc(hole(19, "d"), hole(28, "d"), -22), [hole(19, "d"), hole(28, "d")], (cx(24), 449))

    # level shifter power
    a, b = hole(55, "j"), hole(55, "B+")
    wire("3v3", 22, f"M{a[0]},{a[1]} L{b[0]},{b[1]}", [a, b], (a[0] + 16, 650))
    a, b = hole(54, "j"), hole(54, "B-")
    wire("gnd", 23, f"M{a[0]},{a[1]} L{b[0]},{b[1]}", [a, b], (a[0] - 16, 676))
    a, b = hole(55, "a"), hole(55, "T+")
    wire("5v", 25, f"M{a[0]},{a[1]} L{b[0]},{b[1]}", [a, b], (a[0] + 16, 380))
    a, b = hole(54, "a"), hole(54, "T-")
    wire("gnd", 26, f"M{a[0]},{a[1]} L{b[0]},{b[1]}", [a, b], (a[0] - 16, 380))

    # LED strip + PSU (strip input pads at x=1544, y=119/134/149)
    a = hole(61, "a")
    wire("led", 28, rounded([a, (a[0], 280), (1600, 280), (1600, 134), (1544, 134)]), [a, (1544, 134)], (1600, 220))
    b = hole(60, "T+")
    wire("5v", 29, rounded([(1544, 119), (b[0], 119), b]), [(1544, 119), b], (1620, 180))
    b = hole(62, "T-")
    wire("gnd", 30, rounded([(1544, 149), (1580, 149), (1580, 236), (1660, 236), b]), [(1544, 149), b], (1660, 290))
    b = hole(63, "T+")
    wire("5v", 32, rounded([(1746, 233), (1746, 300), (b[0], 300), b]), [(1746, 233), b], (1712, 300))
    b = hole(63, "T-")
    wire("gnd", 33, rounded([(1774, 233), (1774, ROW["T-"]), b]), [(1774, 233), b], (1774, 300))


def draw_badges():
    for num, (x, y), kind in badges:
        add(f'<circle cx="{x}" cy="{y}" r="10" fill="#ffffff" stroke="{DARK[kind]}" stroke-width="2.5"/>')
        text(x, y + 4, num, 11, "#111", "middle", 700)


# ---------------------------------------------------------------- steps panel
STEPS = [
    ("Power", [
        ("1", "ESP32 3V3 → bottom + rail (this rail = 3.3 V)"),
        ("2", "ESP32 GND → bottom − rail"),
        ("3", "top − rail ↔ bottom − rail (one common GND)"),
    ]),
    ("OLED", [
        ("4", "VCC → bottom + rail"), ("5", "GND → bottom − rail"),
        ("6", "SCL ← ESP32 GPIO41"), ("7", "SDA ← ESP32 GPIO40"),
    ]),
    ("BH1750", [
        ("8", "VCC → + rail"), ("9", "GND → − rail"),
        ("10", "SCL ↔ OLED SCL"), ("11", "SDA ↔ OLED SDA"), ("–", "ADDR: leave empty → 0x23"),
    ]),
    ("SGP30", [
        ("12", "VIN → + rail"), ("13", "GND → − rail"),
        ("14", "SCL ↔ BH1750 SCL"), ("15", "SDA ↔ BH1750 SDA"),
    ]),
    ("DHT22", [("16", "+ → + rail"), ("17", "− → − rail"), ("18", "OUT ← ESP32 GPIO42")]),
    ("MAX4466", [("19", "VCC → + rail"), ("20", "GND → − rail"), ("21", "OUT → ESP32 GPIO1")]),
    ("Level shifter", [
        ("22", "LV → bottom + rail (3.3 V)"), ("23", "GND (LV side) → bottom − rail"),
        ("24", "LV1 ← ESP32 GPIO47"), ("25", "HV → top + rail (5 V)"), ("26", "GND (HV side) → top − rail"),
    ]),
    ("LED strip + 5 V", [
        ("27", "330 Ω: HV1 column → column 61"), ("28", "column 61 → strip DIN"),
        ("29", "strip +5V → top + rail"), ("30", "strip GND → top − rail"),
        ("31", "1000 µF: + leg top + rail, − leg top − rail"),
        ("32", "5 V adapter + → top + rail"), ("33", "5 V adapter − → top − rail"),
    ]),
]


def panel():
    y0 = 830
    add(f'<rect x="40" y="{y0}" width="{W - 80}" height="{H - y0 - 30}" rx="12" fill="#fafafa" stroke="#e0e0e0"/>')
    text(64, y0 + 36, "Build it in this order — numbers match the white circles on the wires", 17, "#263238", "start", 700)
    colw, x, y = 440, 64, y0 + 72
    for title, rows in STEPS:
        need = 24 + len(rows) * 21
        if y + need > H - 60:
            x += colw; y = y0 + 72
        text(x, y, title, 14, "#37474f", "start", 700)
        y += 22
        for n, s in rows:
            add(f'<circle cx="{x + 12}" cy="{y - 4}" r="10" fill="#fff" stroke="#607d8b" stroke-width="2"/>')
            text(x + 12, y, n, 10, "#111", "middle", 700)
            text(x + 30, y + 1, s, 13, "#37474f")
            y += 21
        y += 12
    text(64, H - 56, "Two rails, two voltages: bottom rails = 3.3 V from the ESP32 (all sensors); top rails = 5 V from the adapter "
         "(LED strip only). Never feed the strip from the ESP32.",
         12.5, "#546e7a")
    text(64, H - 36, "Pin order can differ between module brands — match the printed labels. If the red/blue rail lines have a gap "
         "in the middle, your rails are split: bridge each rail with a short jumper.", 12.5, "#546e7a")


def main():
    add(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
        f'font-family="Segoe UI, Helvetica, Arial, sans-serif">')
    add(f'<rect width="{W}" height="{H}" fill="#ffffff"/>')
    text(40, 52, "Ortam Aurora — breadboard wiring", 28, "#263238", "start", 700)
    text(40, 80, "ESP32-S3 smart room monitor · same pins as firmware v7", 15, "#607d8b")
    breadboard()
    esp32()
    for f in (oled, bh1750, sgp30, dht22, max4466, strip, psu, capacitor):
        f()
    wires()
    shifter()
    resistor()
    draw_badges()
    panel()
    add("</svg>")
    Path(__file__).with_name("wiring.svg").write_text("\n".join(out), encoding="utf-8")


if __name__ == "__main__":
    main()
