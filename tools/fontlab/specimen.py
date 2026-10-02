"""Font specimen for the T-Deck Pro's 240px-wide 1-bit panel.

Renders each candidate the way the panel would show it (1 bit, real pixel
sizes) under three rasterization modes, to compare by eye.
"""
import sys
from pathlib import Path

import freetype
from PIL import Image, ImageDraw

S = Path(__file__).resolve().parent
REPO = Path(__file__).resolve().parents[2] / "lib/EpdFont/builtinFonts/source"
FONTS = [
    ("Ubuntu (current UI)", REPO / "Ubuntu/Ubuntu-Regular.ttf", REPO / "Ubuntu/Ubuntu-Bold.ttf"),
    ("Noto Sans", REPO / "NotoSans/NotoSans-Regular.ttf", REPO / "NotoSans/NotoSans-Bold.ttf"),
    ("DejaVu Sans", Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
     Path("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf")),
    ("Atkinson Hyperlegible", S / "fonts/AtkinsonHyperlegible-Regular.ttf", S / "fonts/AtkinsonHyperlegible-Bold.ttf"),
    ("Inter", S / "fonts/Inter.ttf", None),
    ("Fira Sans", S / "fonts/FiraSans-Regular.ttf", S / "fonts/FiraSans-Bold.ttf"),
    ("Source Sans 3", S / "fonts/SourceSans3.ttf", None),
    ("IBM Plex Sans", S / "fonts/IBMPlexSans.ttf", None),
]
MODES = [("A: current (>=2/16)", "aa", 2), ("B: mid (>=8/16)", "aa", 8), ("C: FreeType mono", "mono", 0)]
W = 240


def draw_text(img, x, y, path, ppem, text, mode, thresh):
    face = freetype.Face(str(path))
    face.set_pixel_sizes(0, ppem)
    asc = face.size.ascender >> 6
    pen = x
    for ch in text:
        if mode == "mono":
            face.load_char(ch, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
        else:
            face.load_char(ch, freetype.FT_LOAD_RENDER)
        g = face.glyph
        bm = g.bitmap
        ox, oy = pen + g.bitmap_left, y + asc - g.bitmap_top
        for r in range(bm.rows):
            for c in range(bm.width):
                if mode == "mono":
                    on = (bm.buffer[r * bm.pitch + c // 8] >> (7 - c % 8)) & 1
                else:
                    on = (bm.buffer[r * bm.pitch + c] >> 4) >= thresh
                px, py = ox + c, oy + r
                if on and 0 <= px < img.width and 0 <= py < img.height:
                    img.putpixel((px, py), 0)
        pen += g.advance.x >> 6
        if pen >= img.width:
            break
    return (face.size.height >> 6)


def cell(regular, bold, body, small, mode, thresh):
    img = Image.new("1", (W, 74), 1)
    y = 2
    lh = draw_text(img, 4, y, bold or regular, body, "The Shadow Order Books 1-3", mode, thresh)
    y += lh
    lh = draw_text(img, 4, y, regular, small, "Michael Robertson  -  1/10 books", mode, thresh)
    y += lh + 2
    draw_text(img, 4, y, regular, body, "Browse Files  Library  Settings", mode, thresh)
    return img


def main(body: int, small: int) -> None:
    label_h = 14
    cw, ch = W + 12, 74 + label_h
    sheet = Image.new("L", (cw * len(MODES) + 150, ch * len(FONTS) + 24), 255)
    d = ImageDraw.Draw(sheet)
    d.text((4, 4), f"body {body}px / small {small}px em (current UI: ~21px / ~17px)", fill=0)
    for ci, (mname, _, _) in enumerate(MODES):
        d.text((150 + ci * cw, 4), mname, fill=0)
    for ri, (name, reg, bold) in enumerate(FONTS):
        y0 = 24 + ri * ch
        d.text((4, y0 + 30), name, fill=0)
        for ci, (_, mode, thresh) in enumerate(MODES):
            c = cell(reg, bold, body, small, mode, thresh)
            x0 = 150 + ci * cw
            sheet.paste(c.convert("L"), (x0, y0))
            d.rectangle((x0 - 1, y0 - 1, x0 + W, y0 + 74), outline=180)
    out = S / f"specimen_{body}_{small}.png"
    sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(out)
    print(out)


if __name__ == "__main__":
    main(int(sys.argv[1]), int(sys.argv[2]))
