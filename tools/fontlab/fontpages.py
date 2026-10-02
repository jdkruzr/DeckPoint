import sys
from pathlib import Path
from PIL import Image, ImageDraw
sys.path.insert(0, str(Path(__file__).parent))
from specimen import FONTS, draw_text

S = Path(__file__).resolve().parent
OUT = S / "fontpages"; OUT.mkdir(exist_ok=True)
LINES = [
    ("bold", 15, "Library"),
    ("reg", 12, "Recent    Title    Author"),
    ("bold", 15, "The Shadow Order Books 1-3"),
    ("reg", 12, "Michael Robertson"),
    ("bold", 15, "Red Rising 3-Book Bundle"),
    ("reg", 12, "Pierce Brown  -  2023"),
    ("reg", 15, "Browse Files"),
    ("reg", 15, "Settings   File Transfer"),
    ("reg", 13, "13px: The quick brown fox jumps"),
    ("reg", 11, "11px: over the lazy dog 0123456789"),
    ("reg", 17, "17px: Reading text sample"),
]
for i, (name, reg, bold) in enumerate(FONTS, 1):
    img = Image.new("1", (240, 320), 1)
    d = ImageDraw.Draw(img)
    y = 4
    y += draw_text(img, 4, y, reg, 12, f"{i}. {name}", "mono", 0) + 4
    d.line((0, y, 239, y), fill=0); y += 4
    for style, ppem, text in LINES:
        path = (bold or reg) if style == "bold" else reg
        y += draw_text(img, 4, y, path, ppem, text, "mono", 0) + 2
    fname = f"font{i}_{name.split(' (')[0].replace(' ', '')}.bmp"
    img.convert("RGB").save(OUT / fname)
    print(fname)
