"""Ortam Aurora v2 release visuals: threat model + 3-step setup.

Run:  python draw_release_visuals.py   -> writes threat-model.svg and setup-3-steps.svg
next to this file. Render PNGs with headless Chrome (1600x900).
Facts come from README "Security model" and firmware/ortam_aurora/ortam_aurora.ino
(WPA2 12-char AP password, Bearer token, WS token-first within 5 s, POST-only state
changes, per-pairing 6-digit BLE PIN, 120 s WiFiManager portal). OLED strings in the
setup panel are the firmware's own (Turkish) strings.
"""
from pathlib import Path

HERE = Path(__file__).parent
W, H = 1600, 900

BG, CARD, EDGE = "#07080b", "#0d0f15", "#252935"
AMB, GRN, RED = "#ffb547", "#57e08a", "#ff5d5d"
TXT, DIM, DIM2 = "#e9e7e1", "#8f8d87", "#5d5c58"
MONO = "JetBrains Mono, Consolas, monospace"
DISP = "Space Grotesk, Segoe UI, sans-serif"


def esc(s):
    return str(s).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


class Svg:
    def __init__(self):
        self.o = []

    def add(self, s):
        self.o.append(s)

    def t(self, x, y, content, size=14, fill=TXT, anchor="start", weight=400, fam=DISP,
          ls=None, op=None, extra=""):
        """content: str or list of (text, fill, weight[, family]) spans."""
        a = f' letter-spacing="{ls}"' if ls is not None else ""
        a += f' opacity="{op}"' if op is not None else ""
        if isinstance(content, str):
            body = esc(content)
        else:
            parts = []
            for sp in content:
                s, f, wgt = sp[0], sp[1], sp[2]
                fm = f' font-family="{sp[3]}"' if len(sp) > 3 else ""
                parts.append(f'<tspan fill="{f}" font-weight="{wgt}"{fm}>{esc(s)}</tspan>')
            body = "".join(parts)
        self.add(f'<text x="{x}" y="{y}" font-size="{size}" fill="{fill}" text-anchor="{anchor}" '
                 f'font-weight="{weight}" font-family="{fam}"{a}{extra}>{body}</text>')

    def save(self, name):
        svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">'
               + DEFS + "".join(self.o) + "</svg>")
        (HERE / name).write_text(svg, encoding="utf-8")


DEFS = f"""<defs>
<pattern id="dots" width="28" height="28" patternUnits="userSpaceOnUse">
  <circle cx="2" cy="2" r="1" fill="#ffffff" opacity="0.05"/></pattern>
<filter id="glow" filterUnits="userSpaceOnUse" x="0" y="0" width="1600" height="900">
  <feGaussianBlur stdDeviation="3" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
<filter id="blur8" x="-100%" y="-100%" width="300%" height="300%"><feGaussianBlur stdDeviation="8"/></filter>
<filter id="blur30" x="-100%" y="-100%" width="300%" height="300%"><feGaussianBlur stdDeviation="30"/></filter>
<filter id="shadow" x="-20%" y="-20%" width="140%" height="150%">
  <feDropShadow dx="0" dy="10" stdDeviation="14" flood-color="#000" flood-opacity="0.6"/></filter>
<radialGradient id="haloR" cx="50%" cy="50%" r="50%">
  <stop offset="0" stop-color="{RED}" stop-opacity="0.20"/><stop offset="1" stop-color="{RED}" stop-opacity="0"/></radialGradient>
<radialGradient id="haloG" cx="50%" cy="50%" r="50%">
  <stop offset="0" stop-color="{GRN}" stop-opacity="0.20"/><stop offset="1" stop-color="{GRN}" stop-opacity="0"/></radialGradient>
<radialGradient id="haloA" cx="50%" cy="50%" r="50%">
  <stop offset="0" stop-color="{AMB}" stop-opacity="0.22"/><stop offset="1" stop-color="{AMB}" stop-opacity="0"/></radialGradient>
<linearGradient id="spark" x1="0" y1="0" x2="1" y2="1">
  <stop offset="0" stop-color="{AMB}"/><stop offset="1" stop-color="{GRN}"/></linearGradient>
<linearGradient id="trail" x1="0" y1="0" x2="1" y2="0">
  <stop offset="0" stop-color="{AMB}" stop-opacity="0"/><stop offset="1" stop-color="{AMB}" stop-opacity="1"/></linearGradient>
<linearGradient id="screen" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0" stop-color="#11141c"/><stop offset="1" stop-color="#0a0c11"/></linearGradient>
<marker id="mRed" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
  <path d="M0,0 L10,5 L0,10 z" fill="{RED}"/></marker>
<marker id="mGrn" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
  <path d="M0,0 L10,5 L0,10 z" fill="{GRN}"/></marker>
<marker id="mAmb" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
  <path d="M0,0 L10,5 L0,10 z" fill="{AMB}"/></marker>
</defs>"""


def background(s):
    s.add(f'<rect width="{W}" height="{H}" fill="{BG}"/>')
    s.add(f'<rect width="{W}" height="{H}" fill="url(#dots)"/>')


# ------------------------------------------------------------------ shared parts
def device(s, ox, oy, sc, oled_lines, oled_colors=None):
    """ESP32-S3 DevKitC-1 + SSD1306 OLED + WS2812B strip. Local bbox x -10..310, y 0..336."""
    s.add(f'<g transform="translate({ox} {oy}) scale({sc})">')
    # OLED module
    s.add('<rect x="40" y="0" width="220" height="130" rx="8" fill="#14307a" stroke="#2f56b8" stroke-width="2"/>')
    for hx, hy in ((50, 10), (250, 10), (50, 120), (250, 120)):
        s.add(f'<circle cx="{hx}" cy="{hy}" r="4" fill="{BG}" stroke="#c9a227" stroke-width="1.5"/>')
    s.add('<rect x="58" y="14" width="184" height="94" rx="4" fill="#05060a" stroke="#4a5262" stroke-width="3"/>')
    oled_colors = oled_colors or ["#7fe3ff"] * len(oled_lines)
    n = len(oled_lines)
    for i, (ln, c) in enumerate(zip(oled_lines, oled_colors)):
        y = 14 + 94 * (i + 0.5) / n + 5
        s.t(150, round(y, 1), ln, 13, c, "middle", 600, MONO, extra=' filter="url(#glow)"')
    for i, lab in enumerate(["GND", "VCC", "SCL", "SDA"]):
        px = 126 + i * 16
        s.add(f'<rect x="{px - 4}" y="118" width="8" height="10" fill="#d4af37"/>')
    # jumper wires OLED -> board
    for i, col in enumerate(["#5b606b", "#e53935", "#fdd835", "#1e88e5"]):
        px = 126 + i * 16
        bx = 196 + i * 13
        s.add(f'<path d="M{px},128 C{px},146 {bx},146 {bx},166" fill="none" stroke="{col}" stroke-width="3.2" stroke-linecap="round"/>')
    # DevKitC-1 board
    s.add('<rect x="0" y="160" width="300" height="115" rx="8" fill="#1b1c20" stroke="#000" stroke-width="2"/>')
    for hy in (166, 259):
        s.add(f'<rect x="10" y="{hy}" width="280" height="10" rx="2" fill="#0c0c0e"/>')
        for k in range(21):
            s.add(f'<rect x="{14 + k * 13}" y="{hy + 2}" width="6" height="6" fill="#c9a227"/>')
    s.add('<rect x="22" y="184" width="130" height="68" rx="4" fill="#2b2b2e"/>')
    s.add('<polyline points="30,190 30,246 44,246 44,198 52,198 52,240" fill="none" stroke="#c9a227" stroke-width="2.5"/>')
    s.add('<rect x="60" y="188" width="88" height="60" rx="3" fill="#c7cbd1" stroke="#9aa0a6"/>')
    s.t(104, 216, "ESP32-S3", 13, "#37474f", "middle", 700, DISP)
    s.t(104, 232, "WROOM-1", 9, "#37474f", "middle", 500, DISP)
    s.add('<rect x="176" y="196" width="10" height="10" rx="2" fill="#fff6e0" filter="url(#glow)"/>')
    s.t(181, 192, "RGB", 7, "#9e9e9e", "middle", 400, DISP)
    for bx, lab in ((214, "BOOT"), (256, "RST")):
        s.add(f'<rect x="{bx - 11}" y="214" width="22" height="16" rx="3" fill="#bdbdbd"/>'
              f'<circle cx="{bx}" cy="222" r="4.5" fill="#424242"/>')
        s.t(bx, 243, lab, 7.5, "#bdbdbd", "middle", 400, DISP)
    s.add('<rect x="288" y="206" width="20" height="24" rx="6" fill="#9e9e9e" stroke="#616161"/>'
          '<rect x="294" y="211" width="8" height="14" rx="3" fill="#424242"/>')
    s.t(196, 203, "DevKitC-1", 8, "#8a8a8a", "start", 500, DISP)
    # WS2812B strip
    sunrise = ["#ff3b2f", "#ff5326", "#ff6f2a", "#ff8c35", "#ffb547", "#ffcb73", "#ffdea0", "#ffefcf"]
    s.add('<rect x="-10" y="300" width="320" height="36" rx="5" fill="#17181d" stroke="#30333c" stroke-width="1.5"/>')
    for k, c in enumerate(sunrise):
        lx = 6 + k * 38
        s.add(f'<circle cx="{lx + 11}" cy="318" r="16" fill="{c}" opacity="0.55" filter="url(#blur8)"/>')
        s.add(f'<rect x="{lx}" y="307" width="22" height="22" rx="3" fill="#f2f0ea"/>')
        s.add(f'<circle cx="{lx + 11}" cy="318" r="6.5" fill="{c}"/>')
        s.add(f'<rect x="{lx + 26}" y="314" width="6" height="8" fill="#b08d3c"/>')
    s.add('</g>')


def wifi_icon(s, cx, cy, sc, col, sw=3):
    for r in (1.0, 0.68, 0.36):
        rr = 22 * r * sc
        s.add(f'<path d="M{cx - rr * 0.82},{cy - rr * 0.58} A{rr},{rr} 0 0 1 {cx + rr * 0.82},{cy - rr * 0.58}" '
              f'fill="none" stroke="{col}" stroke-width="{sw}" stroke-linecap="round"/>')
    s.add(f'<circle cx="{cx}" cy="{cy}" r="{3 * sc}" fill="{col}"/>')


def bt_icon(s, cx, cy, sc, col, sw=3):
    p = [(-7, -8), (7, 6), (0, 13), (0, -13), (7, -6), (-7, 8)]
    pts = " ".join(f"{cx + x * sc},{cy + y * sc}" for x, y in p)
    s.add(f'<polyline points="{pts}" fill="none" stroke="{col}" stroke-width="{sw}" stroke-linejoin="round" stroke-linecap="round"/>')


def lock_icon(s, cx, cy, col, open_=False, sc=1.0):
    bw, bh = 16 * sc, 12 * sc
    shk = (f'M{cx - 5 * sc},{cy - 2 * sc} V{cy - 8 * sc} A{5 * sc},{5 * sc} 0 0 1 {cx + 5 * sc},{cy - 8 * sc}'
           + (f' V{cy - 5 * sc}' if not open_ else ''))
    if open_:
        shk = (f'M{cx - 5 * sc},{cy - 2 * sc} V{cy - 8 * sc} A{5 * sc},{5 * sc} 0 0 1 {cx + 5 * sc},{cy - 9 * sc}')
        shk = f'<path d="{shk}" fill="none" stroke="{col}" stroke-width="{2.4 * sc}" stroke-linecap="round" transform="rotate(-28 {cx + 5 * sc} {cy - 2 * sc})"/>'
    else:
        shk = (f'<path d="M{cx - 5 * sc},{cy - 2 * sc} V{cy - 8 * sc} A{5 * sc},{5 * sc} 0 0 1 {cx + 5 * sc},{cy - 8 * sc} '
               f'V{cy - 2 * sc}" fill="none" stroke="{col}" stroke-width="{2.4 * sc}"/>')
    s.add(shk)
    s.add(f'<rect x="{cx - bw / 2}" y="{cy - 2 * sc}" width="{bw}" height="{bh}" rx="{2.5 * sc}" fill="{col}"/>')
    s.add(f'<circle cx="{cx}" cy="{cy + 3.5 * sc}" r="{1.8 * sc}" fill="{BG}"/>')


def house_icon(s, cx, cy, col):
    s.add(f'<path d="M{cx - 17},{cy - 2} L{cx},{cy - 17} L{cx + 17},{cy - 2} M{cx - 12},{cy - 6} V{cy + 15} H{cx + 12} V{cy - 6}" '
          f'fill="none" stroke="{col}" stroke-width="3" stroke-linejoin="round" stroke-linecap="round"/>')
    wifi_icon(s, cx, cy + 10, 0.42, col, 2.2)


def browser_icon(s, cx, cy, col):
    s.add(f'<rect x="{cx - 19}" y="{cy - 15}" width="38" height="30" rx="4" fill="none" stroke="{col}" stroke-width="2.6"/>')
    s.add(f'<line x1="{cx - 19}" y1="{cy - 6}" x2="{cx + 19}" y2="{cy - 6}" stroke="{col}" stroke-width="2.2"/>')
    for k in range(3):
        s.add(f'<circle cx="{cx - 14 + k * 5}" cy="{cy - 10.5}" r="1.4" fill="{col}"/>')
    s.t(cx, cy + 10, "</>", 12, col, "middle", 700, MONO)


def ap_icon(s, cx, cy, col):
    wifi_icon(s, cx, cy + 6, 0.85, col, 2.8)
    s.t(cx, cy + 19, "AP", 9, col, "middle", 700, MONO)


def bt_range_icon(s, cx, cy, col):
    bt_icon(s, cx - 4, cy, 0.95, col, 2.6)
    for r in (10, 16):
        s.add(f'<path d="M{cx + 4 + r * 0.5},{cy - r * 0.86} A{r},{r} 0 0 1 {cx + 4 + r * 0.5},{cy + r * 0.86}" '
              f'fill="none" stroke="{col}" stroke-width="2" stroke-linecap="round" opacity="0.8"/>')


ICONS = {"a": ap_icon, "b": house_icon, "c": browser_icon, "d": bt_range_icon}


# ------------------------------------------------------------------ 1) threat model
def threat_model():
    s = Svg()
    background(s)
    s.add(f'<ellipse cx="330" cy="460" rx="420" ry="360" fill="url(#haloR)"/>')
    s.add(f'<ellipse cx="1270" cy="460" rx="420" ry="360" fill="url(#haloG)"/>')

    # header
    s.t(40, 58, [("Ortam Aurora v2", TXT, 700), (" — hardened", AMB, 700)], 34, TXT, weight=700)
    s.t(40, 88, "Threat model · four ways in, and the control that now sits on each one", 15, DIM, fam=MONO)
    # legend
    lx = 1170
    s.add(f'<line x1="{lx}" y1="52" x2="{lx + 46}" y2="52" stroke="{RED}" stroke-width="2.5" stroke-dasharray="7 5" marker-end="url(#mRed)"/>')
    s.t(lx + 58, 57, "open path in v1", 14, TXT, fam=DISP)
    s.add(f'<line x1="{lx}" y1="82" x2="{lx + 46}" y2="82" stroke="{GRN}" stroke-width="2.5"/>')
    s.add(f'<circle cx="{lx + 23}" cy="82" r="10" fill="{BG}" stroke="{GRN}" stroke-width="1.6"/>')
    lock_icon(s, lx + 23, 83, GRN, sc=0.55)
    s.t(lx + 58, 87, "gated in v2", 14, TXT, fam=DISP)

    # column headers
    def pill(x, y, label, col, anchor):
        w = len(label) * 9.6 + 34
        x0 = x if anchor == "start" else x - w
        s.add(f'<rect x="{x0}" y="{y - 21}" width="{w}" height="32" rx="16" fill="{col}" fill-opacity="0.12" stroke="{col}" stroke-opacity="0.6"/>')
        s.add(f'<circle cx="{x0 + 17}" cy="{y - 5}" r="4.5" fill="{col}"/>')
        s.t(x0 + 30, y, label, 14, col, "start", 700, MONO, ls=1.2)
    pill(40, 140, "V1 · FIELD BUILD", RED, "start")
    pill(1560, 140, "V2 · HARDENED", GRN, "end")
    s.t(800, 140, "ONE DEVICE · FOUR WAYS IN", 13, DIM, "middle", 600, MONO, ls=2)
    s.add(f'<line x1="800" y1="152" x2="800" y2="236" stroke="{EDGE}" stroke-width="1.5" stroke-dasharray="3 5"/>')

    rows = [
        ("a", "Setup-portal range", "the device's own Wi-Fi AP, while unconfigured",
         ["Joins the OPEN setup AP — no password"],
         "Locked setup AP",
         [[("Random 12-char WPA2 password per device", TXT, 400)],
          [("shown on the OLED", GRN, 600), (" when the portal opens", DIM, 400)]]),
        ("b", "Same home Wi-Fi", "anyone on your LAN · HTTP + WebSocket, no auth",
         ["Switches the LEDs · TURNS OFF THE ALARM", "Reads room sensors → presence / sleep"],
         "Per-device API token",
         [[("HTTP  ", DIM, 400, MONO), ("Authorization: Bearer <token>", GRN, 600, MONO)],
          [("WS  ", DIM, 400, MONO), ("1st message = token, else dropped in 5 s", TXT, 400, MONO)]]),
        ("c", "Malicious web page", "open in any browser on your LAN",
         ["State changes were plain GET links:", "a page could flip LEDs / alarm off"],
         "State changes POST-only",
         [[("LED + alarm endpoints refuse GET", TXT, 400)],
          [("and still need the token", GRN, 600), (" — a stray link does nothing", DIM, 400)]]),
        ("d", "Bluetooth range 10–30 m", "anyone within radio range of the room",
         ["Unauthenticated BLE reads + writes:", "sensor data, LED + alarm control"],
         "Paired, encrypted BLE",
         [[("Fresh 6-digit PIN", GRN, 600), (" on the OLED, every pairing", TXT, 400)],
          [("all 4 characteristics: encrypted + authenticated", TXT, 400)]]),
    ]
    centers = [240, 400, 560, 720]
    targets = [300, 400, 500, 600]
    CW, CH = 470, 132
    LX, RX = 40, 1090

    # device enclosure + halo
    s.add('<ellipse cx="800" cy="440" rx="230" ry="250" fill="url(#haloA)"/>')
    s.add(f'<rect x="620" y="250" width="360" height="380" rx="26" fill="#0b0d12" stroke="#2c303b" stroke-width="2" filter="url(#shadow)"/>')
    s.add(f'<rect x="628" y="258" width="344" height="364" rx="20" fill="none" stroke="#ffffff" stroke-opacity="0.04"/>')
    device(s, 650, 272, 1.0, ["Ortam Aurora", "23.4°C   45%", "Lux 120 eCO2 410", "06:30 alarm on"],
           [AMB, "#7fe3ff", "#7fe3ff", "#7fe3ff"])
    s.t(800, 660, "ESP32-S3 · SSD1306 OLED · WS2812B × 60", 13, DIM, "middle", 500, MONO)

    for (key, title, sub, v1, v2title, v2lines), cy, ty in zip(rows, centers, targets):
        top = cy - CH / 2
        # ---- left card (v1)
        s.add(f'<rect x="{LX}" y="{top}" width="{CW}" height="{CH}" rx="16" fill="{CARD}" stroke="{RED}" stroke-opacity="0.35" stroke-width="1.5"/>')
        s.add(f'<rect x="{LX}" y="{top + 18}" width="4" height="{CH - 36}" rx="2" fill="{RED}"/>')
        icx = LX + 56
        s.add(f'<circle cx="{icx}" cy="{cy}" r="32" fill="{RED}" fill-opacity="0.10" stroke="{RED}" stroke-opacity="0.5"/>')
        ICONS[key](s, icx, cy, RED)
        s.add(f'<circle cx="{icx - 26}" cy="{cy - 26}" r="12" fill="{BG}" stroke="{RED}" stroke-width="2"/>')
        s.t(icx - 26, cy - 21.5, key, 13, RED, "middle", 700, MONO)
        tx = LX + 106
        s.t(tx, top + 34, title, 19, TXT, weight=700)
        s.t(tx, top + 56, sub, 12.5, DIM, fam=MONO)
        for i, ln in enumerate(v1):
            yy = top + 86 + i * 24
            s.add(f'<path d="M{tx + 1},{yy - 9} l8,8 m0,-8 l-8,8" stroke="{RED}" stroke-width="2.2" stroke-linecap="round"/>')
            s.t(tx + 18, yy, ln, 15, "#ff8f8f", weight=600)
        # red dashed attack path into the device
        x0, x1 = LX + CW, 620
        s.add(f'<path d="M{x0},{cy} C{x0 + 70},{cy} {x1 - 60},{ty} {x1 - 3},{ty}" fill="none" stroke="{RED}" '
              f'stroke-width="2.6" stroke-dasharray="8 6" marker-end="url(#mRed)" filter="url(#glow)"/>')
        mx, my = (x0 + x1) / 2, (cy + ty) / 2
        s.add(f'<circle cx="{mx}" cy="{my}" r="13" fill="{BG}" stroke="{RED}" stroke-width="1.6"/>')
        lock_icon(s, mx, my + 1, RED, open_=True, sc=0.7)

        # ---- right card (v2)
        s.add(f'<rect x="{RX}" y="{top}" width="{CW}" height="{CH}" rx="16" fill="{CARD}" stroke="{GRN}" stroke-opacity="0.35" stroke-width="1.5"/>')
        s.add(f'<rect x="{RX + CW - 4}" y="{top + 18}" width="4" height="{CH - 36}" rx="2" fill="{GRN}"/>')
        icx = RX + 56
        s.add(f'<circle cx="{icx}" cy="{cy}" r="32" fill="{GRN}" fill-opacity="0.10" stroke="{GRN}" stroke-opacity="0.5"/>')
        ICONS[key](s, icx, cy, GRN)
        s.add(f'<circle cx="{icx + 24}" cy="{cy + 24}" r="12" fill="{BG}" stroke="{GRN}" stroke-width="1.6"/>')
        lock_icon(s, icx + 24, cy + 25, GRN, sc=0.62)
        s.add(f'<circle cx="{icx - 26}" cy="{cy - 26}" r="12" fill="{BG}" stroke="{GRN}" stroke-width="2"/>')
        s.t(icx - 26, cy - 21.5, key, 13, GRN, "middle", 700, MONO)
        tx = RX + 106
        s.t(tx, top + 34, v2title, 19, TXT, weight=700)
        for i, spans in enumerate(v2lines):
            yy = top + 66 + i * 26
            mono = any(len(sp) > 3 for sp in spans)
            s.t(tx, yy, spans, 13.5 if mono else 14.5, TXT, fam=MONO if mono else DISP)
        # green gated path
        x0, x1 = RX, 980
        s.add(f'<path d="M{x0},{cy} C{x0 - 50},{cy} {x1 + 50},{ty} {x1},{ty}" fill="none" stroke="{GRN}" '
              f'stroke-width="2.6" filter="url(#glow)"/>')
        s.add(f'<circle cx="{x1}" cy="{ty}" r="4" fill="{GRN}"/>')
        mx, my = (x0 + x1) / 2, (cy + ty) / 2
        s.add(f'<circle cx="{mx}" cy="{my}" r="15" fill="{BG}" stroke="{GRN}" stroke-width="1.8"/>')
        lock_icon(s, mx, my + 1, GRN, sc=0.8)

    # footer
    s.add(f'<rect x="40" y="808" width="1520" height="56" rx="14" fill="{CARD}" stroke="{EDGE}"/>')
    s.t(64, 842, [("RESIDUAL  ", AMB, 700), ("OTA page during 120 s setup portal", TXT, 400),
                  ("  ·  ", DIM2, 400), ("no TLS on LAN", TXT, 400), ("  ·  ", DIM2, 400),
                  ("independent review: ", DIM, 400), ("passed with fixes", GRN, 700)], 15, TXT, fam=MONO)
    s.t(1536, 842, "trusted-LAN device · keep it off the internet", 13, DIM, "end", 400, MONO)
    s.save("threat-model.svg")


# ------------------------------------------------------------------ 2) setup in 3 steps
def phone(s, x, y, w, h):
    s.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="30" fill="#14161c" stroke="#3a3e4a" stroke-width="3" filter="url(#shadow)"/>')
    s.add(f'<rect x="{x + 9}" y="{y + 9}" width="{w - 18}" height="{h - 18}" rx="22" fill="url(#screen)"/>')
    s.add(f'<rect x="{x + w / 2 - 26}" y="{y + 18}" width="52" height="12" rx="6" fill="#000"/>')
    s.t(x + 28, y + 30, "21:47", 10, TXT, "start", 600, MONO)
    wifi_icon(s, x + w - 46, y + 31, 0.38, TXT, 1.8)
    s.add(f'<rect x="{x + w - 36}" y="{y + 22}" width="16" height="8" rx="2" fill="none" stroke="{TXT}" stroke-width="1.2"/>'
          f'<rect x="{x + w - 34}" y="{y + 24}" width="9" height="4" fill="{TXT}"/>')
    return x + 9, y + 9, w - 18, h - 18


def badge(s, cx, cy, n):
    s.add(f'<circle cx="{cx}" cy="{cy}" r="26" fill="{AMB}" fill-opacity="0.14" stroke="{AMB}" stroke-width="2"/>')
    s.add(f'<circle cx="{cx}" cy="{cy}" r="34" fill="none" stroke="{AMB}" stroke-opacity="0.25" stroke-width="1"/>')
    s.t(cx, cy + 9, str(n), 26, AMB, "middle", 700, DISP)


def connector(s, x0, x1, y):
    s.add(f'<rect x="{x0}" y="{y - 2}" width="{x1 - x0 - 14}" height="4" rx="2" fill="url(#trail)" filter="url(#glow)"/>')
    for k, op in enumerate((0.35, 0.65, 1.0)):
        cx = x0 + 16 + k * 16
        s.add(f'<path d="M{cx},{y - 10} l9,10 l-9,10" fill="none" stroke="{AMB}" stroke-width="3" '
              f'stroke-linecap="round" stroke-linejoin="round" opacity="{op}"/>')
    s.add(f'<circle cx="{x1 - 6}" cy="{y}" r="5" fill="{AMB}" filter="url(#glow)"/>')


def setup_steps():
    s = Svg()
    background(s)
    s.add(f'<ellipse cx="800" cy="470" rx="760" ry="330" fill="url(#haloA)" opacity="0.6"/>')
    s.t(40, 58, [("Ortam Aurora v2", TXT, 700), (" — set up in 3 steps", AMB, 700)], 34, TXT, weight=700)
    s.t(40, 88, "From power-on to the live dashboard · every secret is read off the device's own screen", 15, DIM, fam=MONO)

    PW, PT, PB = 460, 120, 800
    xs = [40, 570, 1100]
    for x in xs:
        s.add(f'<rect x="{x}" y="{PT}" width="{PW}" height="{PB - PT}" rx="22" fill="{CARD}" stroke="{EDGE}" stroke-width="1.5"/>')
        s.add(f'<rect x="{x + 24}" y="{PT}" width="{PW - 48}" height="2" fill="{AMB}" opacity="0.5"/>')
    connector(s, 500, 570, 470)
    connector(s, 1030, 1100, 470)

    heads = [
        ("Device opens its own Wi-Fi", ["“OrtamMonitor-Setup” (WPA2)", "the password is on the OLED"]),
        ("Connect from your phone", ["captive “Sign in” page →", "pick your home Wi-Fi — saved"]),
        ("Open the live dashboard", ["browser → http://<IP on the OLED>/", "paste the API token once"]),
    ]
    for i, (x, (title, lines)) in enumerate(zip(xs, heads)):
        badge(s, x + 58, PT + 62, i + 1)
        s.t(x + 108, PT + 56, title, 23, TXT, weight=700)
        for k, ln in enumerate(lines):
            s.t(x + 108, PT + 82 + k * 21, ln, 14, DIM, fam=MONO)

    # ---- panel 1: device in AP mode
    x = xs[0]
    sc = 1.12
    dx = x + PW / 2 - 150 * sc
    dy = 330
    s.add(f'<ellipse cx="{x + PW / 2}" cy="{dy + 190}" rx="200" ry="210" fill="url(#haloA)"/>')
    device(s, dx, dy, sc, ["Setup AP open", "OrtamMonitor-Setup", "Password:", "EXAMPLE-PASS"],
           ["#7fe3ff", AMB, "#7fe3ff", GRN])
    # radio waves + SSID chip
    wx, wy = x + PW / 2, 300
    for r, op in ((26, 0.9), (44, 0.55), (62, 0.3)):
        s.add(f'<path d="M{wx - r * 0.8},{wy - r * 0.45} A{r},{r} 0 0 1 {wx + r * 0.8},{wy - r * 0.45}" fill="none" '
              f'stroke="{AMB}" stroke-width="3" stroke-linecap="round" opacity="{op}"/>')
    s.add(f'<circle cx="{wx}" cy="{wy}" r="4" fill="{AMB}"/>')
    s.t(x + PW / 2, dy + 418, [("OLED, firmware text: ", DIM, 400), ("“setup AP open · password:”", TXT, 400)], 12.5, TXT, "middle", fam=MONO)
    s.t(x + PW / 2, dy + 440, "12 random chars, unique per device", 12.5, GRN, "middle", 600, MONO)

    # ---- panel 2: phone with captive portal
    x = xs[1]
    pw_, ph_ = 236, 470
    px, py = x + PW / 2 - pw_ / 2, 295
    sx, sy, sw, sh = phone(s, px, py, pw_, ph_)
    # captive notification sheet
    s.add(f'<rect x="{sx + 10}" y="{sy + 34}" width="{sw - 20}" height="52" rx="12" fill="#1c2029" stroke="{AMB}" stroke-opacity="0.5"/>')
    wifi_icon(s, sx + 32, sy + 66, 0.5, AMB, 2.2)
    s.t(sx + 52, sy + 56, "Sign in to network", 12, TXT, "start", 700, DISP)
    s.t(sx + 52, sy + 74, "OrtamMonitor-Setup", 10.5, AMB, "start", 600, MONO)
    # portal page
    s.t(sx + sw / 2, sy + 116, "Choose your Wi-Fi", 14, TXT, "middle", 700, DISP)
    nets = [("MyHome-WiFi", True, 3), ("Neighbour-5G", False, 2), ("Cafe_Guest", False, 1)]
    for k, (nm, sel, bars) in enumerate(nets):
        ny = sy + 132 + k * 42
        s.add(f'<rect x="{sx + 12}" y="{ny}" width="{sw - 24}" height="34" rx="8" fill="{"#2a2213" if sel else "#161920"}" '
              f'stroke="{AMB if sel else "#2a2e38"}" stroke-width="{1.6 if sel else 1}"/>')
        s.t(sx + 26, ny + 22, nm, 12, TXT if sel else DIM, "start", 600 if sel else 400, MONO)
        for b in range(3):
            bh = 5 + b * 4
            s.add(f'<rect x="{sx + sw - 44 + b * 7}" y="{ny + 26 - bh}" width="4" height="{bh}" rx="1" '
                  f'fill="{(AMB if sel else DIM) if b < bars else "#333"}"/>')
    fy = sy + 266
    s.t(sx + 14, fy, "Password", 10, DIM, "start", 500, DISP)
    s.add(f'<rect x="{sx + 12}" y="{fy + 7}" width="{sw - 24}" height="32" rx="8" fill="#0b0d12" stroke="#3a3e4a"/>')
    s.t(sx + 24, fy + 29, "• • • • • • • • • •", 12, TXT, "start", 700, MONO)
    s.add(f'<rect x="{sx + 12}" y="{fy + 52}" width="{sw - 24}" height="36" rx="18" fill="{AMB}"/>')
    s.t(sx + sw / 2, fy + 75, "Save", 14, "#1a1206", "middle", 700, DISP)
    s.add(f'<circle cx="{sx + sw / 2}" cy="{fy + 120}" r="14" fill="{GRN}" fill-opacity="0.15" stroke="{GRN}" stroke-width="1.6"/>')
    s.add(f'<path d="M{sx + sw / 2 - 6},{fy + 120} l4,5 l9,-10" fill="none" stroke="{GRN}" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"/>')
    s.t(sx + sw / 2, fy + 152, "saved in flash", 11.5, GRN, "middle", 600, MONO)
    s.t(sx + sw / 2, fy + 170, "never asked again", 11.5, DIM, "middle", 400, MONO)
    # joined chip above the phone
    s.add(f'<rect x="{x + PW / 2 - 130}" y="248" width="260" height="32" rx="16" fill="{AMB}" fill-opacity="0.1" stroke="{AMB}" stroke-opacity="0.6"/>')
    lock_icon(s, x + PW / 2 - 108, 266, AMB, sc=0.6)
    s.t(x + PW / 2 - 92, 269, "joined OrtamMonitor-Setup", 12.5, AMB, "start", 600, MONO)

    # ---- panel 3: browser dashboard (what v2 serves today) + companion-app status tag
    x = xs[2]
    pw_, ph_ = 236, 470
    px, py = x + PW - pw_ - 22, 295
    sx, sy, sw, sh = phone(s, px, py, pw_, ph_)
    # mini device (left top): OLED after Wi-Fi joins (firmware strings) -> live data to the browser
    msc = 0.5
    mdx, mdy = x + 23, 292
    s.add(f'<ellipse cx="{mdx + 75}" cy="{mdy + 84}" rx="95" ry="95" fill="url(#haloA)"/>')
    device(s, mdx, mdy, msc, ["WiFi connected!", "IP: 192.168.1.42", "BLE: active", "Alarm: 06:30 ON"],
           [GRN, AMB, "#7fe3ff", "#7fe3ff"])
    s.add(f'<path d="M{mdx + 75},{mdy + 178} C{mdx + 75},{mdy + 232} {mdx + 120},{mdy + 226} {px - 4},{mdy + 226}" fill="none" '
          f'stroke="{AMB}" stroke-width="2.8" stroke-dasharray="2 7" stroke-linecap="round" marker-end="url(#mAmb)" filter="url(#glow)"/>')
    s.t(mdx + 65, mdy + 200, "WebSocket", 11.5, AMB, "end", 600, MONO)
    s.t(mdx + 65, mdy + 216, "every 500 ms", 11.5, AMB, "end", 600, MONO)
    # companion app status tag (left bottom) -- not part of today's path
    tx0, ty0, tw0, th0 = x + 20, 590, px - x - 36, 116
    s.add(f'<rect x="{tx0}" y="{ty0}" width="{tw0}" height="{th0}" rx="14" fill="#11131a" stroke="{DIM2}" '
          f'stroke-width="1.4" stroke-dasharray="5 5"/>')
    s.add(f'<rect x="{tx0 + 14}" y="{ty0 + 16}" width="22" height="36" rx="5" fill="none" stroke="{DIM}" stroke-width="1.6"/>'
          f'<rect x="{tx0 + 21}" y="{ty0 + 20}" width="8" height="2" rx="1" fill="{DIM}"/>')
    s.t(tx0 + 46, ty0 + 30, "companion app", 12, TXT, "start", 700, MONO)
    s.t(tx0 + 46, ty0 + 47, "token/PIN update", 11, DIM, "start", 400, MONO)
    s.add(f'<rect x="{tx0 + 14}" y="{ty0 + 70}" width="{tw0 - 28}" height="26" rx="13" fill="{AMB}" fill-opacity="0.1" '
          f'stroke="{AMB}" stroke-opacity="0.55"/>')
    s.t(tx0 + tw0 / 2, ty0 + 88, "update in progress", 11, AMB, "middle", 600, MONO)
    # phone browser screen (English labels for the firmware's dashboard strings)
    s.add(f'<rect x="{sx + 10}" y="{sy + 32}" width="{sw - 20}" height="26" rx="13" fill="#1c2029" stroke="#2f3440"/>')
    s.t(sx + 22, sy + 49, [("http://", DIM, 400), ("192.168.1.42", TXT, 600)], 10.5, TXT, fam=MONO)
    s.t(sx + 14, sy + 80, "Smart Room Monitor v2", 12.5, TXT, "start", 700, DISP)
    s.add(f'<circle cx="{sx + 18}" cy="{sy + 94}" r="3.5" fill="{GRN}" filter="url(#glow)"/>')
    s.t(sx + 27, sy + 98, "WiFi connected", 9.5, GRN, "start", 600, MONO)
    tiles = [("TEMP", "23.4", "°C"), ("HUMIDITY", "45.0", "%"), ("LIGHT", "120", "lx"),
             ("eCO2", "410", "ppm")]
    tw, th = (sw - 34) / 2, 48
    for k, (lab, val, unit) in enumerate(tiles):
        tx = sx + 12 + (k % 2) * (tw + 10)
        ty = sy + 108 + (k // 2) * (th + 8)
        s.add(f'<rect x="{tx}" y="{ty}" width="{tw}" height="{th}" rx="9" fill="#161920" stroke="#262a34"/>')
        s.t(tx + 9, ty + 17, lab, 8.5, DIM, "start", 600, MONO, ls=0.5)
        s.t(tx + 9, ty + 39, [(val, TXT, 700), (" " + unit, DIM, 400)], 16, TXT, fam=DISP)
    # sound tile (full width with level bars)
    ty = sy + 108 + 2 * (th + 8)
    s.add(f'<rect x="{sx + 12}" y="{ty}" width="{sw - 24}" height="38" rx="9" fill="#161920" stroke="#262a34"/>')
    s.t(sx + 21, ty + 23, "SOUND", 8.5, DIM, "start", 600, MONO, ls=0.5)
    lv = [4, 7, 5, 10, 6, 13, 8, 5, 9, 4, 6, 3, 7, 5, 8, 4]
    for k, v in enumerate(lv):
        s.add(f'<rect x="{sx + 60 + k * 8.5}" y="{ty + 19 - v}" width="4" height="{2 * v}" rx="2" fill="{AMB}" opacity="{0.45 + 0.03 * k}"/>')
    # token chip: asked once, kept in this browser
    ky = ty + 48
    s.add(f'<rect x="{sx + 12}" y="{ky}" width="{sw - 24}" height="30" rx="15" fill="#122018" stroke="{GRN}" stroke-opacity="0.5"/>')
    lock_icon(s, sx + 30, ky + 15, GRN, sc=0.55)
    s.t(sx + 44, ky + 20, "API token ok · asked once", 10, GRN, "start", 600, MONO)
    # LED & Alarm card
    ly = ky + 40
    s.add(f'<rect x="{sx + 12}" y="{ly}" width="{sw - 24}" height="{sy + sh - ly - 16}" rx="12" fill="#161920" stroke="#262a34"/>')
    s.t(sx + 24, ly + 22, "LED & ALARM", 8.5, DIM, "start", 600, MONO, ls=0.5)
    s.add(f'<circle cx="{sx + 34}" cy="{ly + 46}" r="10" fill="{AMB}" filter="url(#glow)"/>')
    s.t(sx + 54, ly + 50, "Alarm 06:30", 12, TXT, "start", 700, MONO)
    s.add(f'<rect x="{sx + sw - 62}" y="{ly + 36}" width="38" height="20" rx="10" fill="{GRN}" fill-opacity="0.2" stroke="{GRN}"/>')
    s.t(sx + sw - 43, ly + 50, "ON", 10, GRN, "middle", 700, MONO)
    # brightness slider (0-160)
    s.t(sx + 24, ly + 80, "Brightness", 9.5, DIM, "start", 500, MONO)
    s.add(f'<rect x="{sx + 24}" y="{ly + 90}" width="{sw - 48}" height="4" rx="2" fill="#2a2e38"/>'
          f'<rect x="{sx + 24}" y="{ly + 90}" width="{(sw - 48) * 0.8}" height="4" rx="2" fill="{AMB}"/>'
          f'<circle cx="{sx + 24 + (sw - 48) * 0.8}" cy="{ly + 92}" r="6" fill="{TXT}"/>')
    s.t(sx + sw - 24, ly + 80, "128", 9.5, TXT, "end", 600, MONO)

    # footer
    s.t(800, 852, [("Setup portal opens only when no known Wi-Fi is reachable", DIM, 400),
                   ("  ·  ", DIM2, 400), ("120 s window", TXT, 400), ("  ·  ", DIM2, 400),
                   ("API token (first boot only) + BLE PIN are shown on the OLED too", DIM, 400)], 13, DIM, "middle", fam=MONO)
    s.save("setup-3-steps.svg")


if __name__ == "__main__":
    threat_model()
    setup_steps()
    print("wrote threat-model.svg, setup-3-steps.svg")
