"""从 res/*.svg 生成 res/png 与 res/esn.ico（PNG-in-ICO），并输出 res/preview-sheet.png。

依赖本机 Chrome（headless 截图）与 Pillow。用法：python tools/build_icons.py
"""
import base64
import pathlib
import struct
import subprocess
import tempfile

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
RES = ROOT / "res"
def find_chrome() -> str:
    import os
    env = os.environ.get("ESN_CHROME")
    if env and os.path.isfile(env):
        return env
    candidates = [
        r"C:\Program Files\Google\Chrome\Application\chrome.exe",
        r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
        os.path.expandvars(r"%LOCALAPPDATA%\Google\Chrome\Application\chrome.exe"),
        r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
    ]
    for path in candidates:
        if os.path.isfile(path):
            return path
    raise SystemExit("找不到 Chrome/Edge，请设置环境变量 ESN_CHROME 指向 chrome.exe")


CHROME = find_chrome()
SIZES = [16, 20, 24, 32, 48, 64, 128, 256]


def page(svg: str, w: int, h: int, color: str = "#000000") -> str:
    return f"""<!doctype html><meta charset="utf-8"><style>
html,body{{margin:0;padding:0;background:transparent;overflow:hidden;color:{color}}}
svg{{display:block;width:{w}px;height:{h}px}}</style>{svg}"""


def shot(html: str, w: int, h: int, dest: pathlib.Path) -> None:
    with tempfile.NamedTemporaryFile("w", suffix=".html", delete=False, encoding="utf-8") as f:
        f.write(html)
        p = pathlib.Path(f.name)
    try:
        subprocess.run(
            [CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--no-first-run",
             "--no-default-browser-check", "--force-device-scale-factor=1",
             "--default-background-color=00000000", f"--window-size={w},{h}",
             f"--screenshot={dest}", p.as_uri()],
            check=True, capture_output=True, timeout=90)
    finally:
        p.unlink(missing_ok=True)


def bmp_entry(img: Image.Image) -> bytes:
    """把 RGBA 图编码成 ICO 里的 32bpp BMP 条目（含 AND 掩码）。"""
    w, h = img.size
    px = img.load()
    rows = []
    for y in range(h - 1, -1, -1):
        row = bytearray()
        for x in range(w):
            r, g, b, a = px[x, y]
            row += bytes((b, g, r, a))
        rows.append(bytes(row))
    pixels = b"".join(rows)
    # AND 掩码：alpha 为 0 的像素标记为透明（老 API 与无 alpha 通道的路径会用到）
    mask_stride = ((w + 31) // 32) * 4
    mask = bytearray(mask_stride * h)
    for y in range(h):
        base = (h - 1 - y) * mask_stride
        for x in range(w):
            if px[x, y][3] == 0:
                mask[base + x // 8] |= 0x80 >> (x % 8)
    header = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0,
                         len(pixels) + len(mask), 0, 0, 0, 0)
    return header + pixels + mask


def build_ico(paths, dest: pathlib.Path) -> None:
    blobs = []
    for p in paths:
        im = Image.open(p).convert("RGBA")
        # ≥128 用 PNG（体积小，Vista+ 支持）；≤64 用 BMP（兼容老 API 与小尺寸渲染）
        blob = p.read_bytes() if im.width >= 128 else bmp_entry(im)
        blobs.append((im.width, im.height, blob))
    header = struct.pack("<HHH", 0, 1, len(blobs))
    offset = 6 + 16 * len(blobs)
    entries, data = b"", b""
    for w, h, blob in blobs:
        entries += struct.pack("<BBBBHHII", w % 256, h % 256, 0, 0, 1, 32, len(blob), offset)
        data += blob
        offset += len(blob)
    dest.write_bytes(header + entries + data)


def main() -> None:
    master = (RES / "esn-icon.svg").read_text(encoding="utf-8")
    tray = (RES / "esn-tray.svg").read_text(encoding="utf-8")
    out = RES / "png"
    out.mkdir(exist_ok=True)

    for s in SIZES:
        shot(page(master, s, s), s, s, out / f"esn-{s}.png")
    for color, tag in (("#FFFFFF", "white"), ("#000000", "black")):
        for s in (16, 20, 32):
            shot(page(tray, s, s, color), s, s, out / f"tray-{tag}-{s}.png")
    build_ico([out / f"esn-{s}.png" for s in SIZES], RES / "esn.ico")

    def uri(p: pathlib.Path) -> str:
        return "data:image/png;base64," + base64.b64encode(p.read_bytes()).decode()

    rows = []
    for s in SIZES:
        rows.append(f'<div class=c><img src="{uri(out / f"esn-{s}.png")}" width="{s}" height="{s}">'
                    f'<span>{s}</span></div>')
    zoom = "".join(
        f'<div class=c><img class=z src="{uri(out / f"esn-{s}.png")}" width="{s*6}" height="{s*6}">'
        f'<span>{s}</span></div>' for s in (16, 24, 32, 48))
    sheet = f"""<!doctype html><meta charset="utf-8"><style>
body{{margin:0;padding:18px;background:#fff;font:11px "Segoe UI";color:#888}}
h3{{margin:0 0 10px;font-size:12px;color:#666;font-weight:600}}
section{{margin-bottom:18px}}
.row{{display:flex;gap:20px;align-items:flex-end}}
.c{{display:flex;flex-direction:column;gap:6px;align-items:center}}
.z{{image-rendering:pixelated;outline:1px solid #eee}}
</style>
<section><h3>actual size</h3><div class=row>{"".join(rows)}</div></section>
<section><h3>6x</h3><div class=row>{zoom}</div></section>"""
    shot(sheet, 900, 300, RES / "preview-sheet.png")
    print("icons ok:", (RES / "esn.ico").stat().st_size, "bytes")


if __name__ == "__main__":
    main()
