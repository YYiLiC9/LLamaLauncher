# Generates res/app.ico - the LlamaLauncher application icon.
# Design: rounded square with a blue->teal vertical gradient, a white "L"
# wordmark and a small play accent, so the icon reads at 16px and still looks
# finished at 256px. Re-run to regenerate: python tools/make_icon.py
from PIL import Image, ImageDraw, ImageFont
import os

S = 1024  # master canvas, downscaled for each size
OUT = os.path.join(os.path.dirname(__file__), '..', 'res', 'app.ico')

BLUE = (21, 84, 205)
TEAL = (0, 168, 150)


def rounded_gradient(size, radius_frac=0.24):
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    grad = Image.new('RGBA', (size, size))
    gd = ImageDraw.Draw(grad)
    top, bottom = BLUE, TEAL
    for y in range(size):
        t = y / (size - 1)
        c = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,)
        gd.line([(0, y), (size, y)], fill=c)
    mask = Image.new('L', (size, size), 0)
    md = ImageDraw.Draw(mask)
    r = int(size * radius_frac)
    md.rounded_rectangle([0, 0, size - 1, size - 1], radius=r, fill=255)
    img.paste(grad, (0, 0), mask)
    return img


def draw_glyph(img):
    d = ImageDraw.Draw(img)
    # White bold "L", optically centred and slightly left to make room for
    # the play accent in the bottom-right corner.
    size = img.size[0]
    font = None
    for cand in ('segoeuib.ttf', 'arialbd.ttf', 'arial.ttf'):
        try:
            font = ImageFont.truetype('C:/Windows/Fonts/' + cand, int(size * 0.62))
            break
        except OSError:
            continue
    if font is None:
        font = ImageFont.load_default()
    bbox = d.textbbox((0, 0), 'L', font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    d.text((size * 0.40 - tw / 2 - bbox[0], size * 0.46 - th / 2 - bbox[1]), 'L',
           font=font, fill=(255, 255, 255, 255))
    # Play accent: a filled circle + triangle in the bottom-right corner.
    cr = size * 0.135
    cx, cy = size * 0.76, size * 0.78
    d.ellipse([cx - cr, cy - cr, cx + cr, cy + cr], fill=(255, 255, 255, 235))
    tr = cr * 0.52
    d.polygon([(cx - tr * 0.55, cy - tr), (cx - tr * 0.55, cy + tr), (cx + tr * 1.05, cy)],
              fill=(0, 150, 136, 255))
    return img


master = rounded_gradient(S)
draw_glyph(master)

sizes = [256, 128, 64, 48, 32, 24, 16]
frames = [master.resize((s, s), Image.LANCZOS) for s in sizes]
frames[0].save(OUT, format='ICO', sizes=[(s, s) for s in sizes], append_images=frames[1:])
print('written', os.path.abspath(OUT))
