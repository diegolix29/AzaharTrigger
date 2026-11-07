"""Generate every Azalon3-DS icon from one SVG.

usage: python -I tools/make-icons.py dist/azalon3-ds-icon-source.svg .
Needs Pillow and rsvg-convert (MSYS2 ucrt64: pacman -S mingw-w64-ucrt-x86_64-librsvg).
"""
import os
import re
import subprocess
import sys
import tempfile

from PIL import Image

RSVG = r"C:\msys64\ucrt64\bin\rsvg-convert.exe"
SRC, REPO = sys.argv[1], sys.argv[2]
TMP = tempfile.mkdtemp()
RENDER = 2048  # render size used as the master for every bitmap
ANDROID_BG = "#5BD9A2"

src_svg = open(SRC, encoding="utf-8").read()


def render(svg_text, size):
    path_in = os.path.join(TMP, "in.svg")
    path_out = os.path.join(TMP, "out.png")
    open(path_in, "w", encoding="utf-8").write(svg_text)
    subprocess.run([RSVG, "-w", str(size), "-h", str(size), "-o", path_out, path_in], check=True)
    return Image.open(path_out).convert("RGBA")


def alpha_bbox(img):
    return img.getchannel("A").point(lambda a: 255 if a > 8 else 0).getbbox()


def fit(art, canvas, frac):
    """art cropped to its content, scaled so its longest side is canvas*frac, centred."""
    w, h = art.size
    s = canvas * frac / max(w, h)
    small = art.resize((max(1, round(w * s)), max(1, round(h * s))), Image.LANCZOS)
    out = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
    out.alpha_composite(small, ((canvas - small.width) // 2, (canvas - small.height) // 2))
    return out


def save(img, rel):
    path = os.path.join(REPO, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path, optimize=True)
    print("wrote", rel, img.size)


# Full-colour artwork and the white silhouette (the "sticker" shapes only).
full = render(src_svg, RENDER)
full_box = alpha_bbox(full)
art = full.crop(full_box)

sil_group = re.search(r'<g fill="#FFFFFF" stroke="#FFFFFF".*?</g>', src_svg, re.S).group(0)
sil_svg = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024">' + sil_group + "</svg>")
sil = render(sil_svg, RENDER)
sil_box = alpha_bbox(sil)
sil_art = sil.crop(sil_box)

# ---- Desktop -------------------------------------------------------------
# Centred SVG (Linux icon): fit the shadowed artwork into 1024 with a 3% margin.
k = 1024 / RENDER
x0, y0, x1, y1 = (v * k for v in full_box)
s = 1024 * 0.94 / max(x1 - x0, y1 - y0)
cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
wrapped = src_svg.replace(
    "</defs>", f'</defs>\n    <g transform="translate(512 512) scale({s:.5f}) translate({-cx:.2f} {-cy:.2f})">', 1)
wrapped = wrapped[: wrapped.rindex("</svg>")] + "    </g>\n</svg>\n"
open(os.path.join(REPO, "dist/azahar.svg"), "w", encoding="utf-8", newline="\n").write(wrapped)
print("wrote dist/azahar.svg")

save(fit(art, 512, 0.94), "dist/azahar.png")
save(fit(art, 256, 0.94), "dist/qt_themes/default/icons/256x256/azahar.png")
save(fit(art, 256, 0.94), "dist/qt_themes/default/icons_light/256x256/azahar.png")
save(fit(art, 512, 0.90), "dist/apple/launch_logo.png")

ico_sizes = [16, 24, 32, 48, 64, 128, 256]
ico_imgs = [fit(art, n, 0.96 if n <= 32 else 0.94) for n in ico_sizes]
ico_imgs[-1].save(os.path.join(REPO, "dist/azahar.ico"), sizes=[(n, n) for n in ico_sizes],
                  append_images=ico_imgs[:-1])
print("wrote dist/azahar.ico", ico_sizes)

# macOS: Apple's grid leaves ~10% padding around the artwork.
fit(art, 1024, 0.82).save(os.path.join(REPO, "dist/apple/azahar.icns"))
print("wrote dist/apple/azahar.icns")

# ---- Android ---------------------------------------------------------------
RES = "src/android/app/src/main/res"
DENS = {"mdpi": 1, "hdpi": 1.5, "xhdpi": 2, "xxhdpi": 3, "xxxhdpi": 4}

# Adaptive icon foreground: 108dp canvas, artwork 60dp so every launcher mask keeps it whole.
for d, m in DENS.items():
    save(fit(art, round(108 * m), 60 / 108), f"{RES}/drawable-{d}/ic_launcher_foreground.png")

# Logo shown inside the app (About, Home settings, setup welcome page): 192dp.
for d, m in DENS.items():
    save(fit(art, round(192 * m), 0.96), f"{RES}/drawable-{d}/ic_citra_full.png")

# Status-bar notification icon: white silhouette, same pixel sizes as before.
for d, n in {"hdpi": 72, "xhdpi": 96, "xxhdpi": 144}.items():
    save(fit(sil_art, n, 0.88), f"{RES}/drawable-{d}/ic_stat_notification_logo.png")

# Monochrome (themed icon) layer: the silhouette shapes as a vector, 60dp inside 108dp.
k = 1024 / RENDER
sx0, sy0, sx1, sy1 = (v * k for v in sil_box)
ms = 1024 * (60 / 108) / max(sx1 - sx0, sy1 - sy0)
tx = 512 - ms * (sx0 + sx1) / 2
ty = 512 - ms * (sy0 + sy1) / 2
paths = re.findall(r'd="([^"]+)"', sil_group)
body = "\n".join(
    f'''        <path
            android:fillColor="#FFFFFF"
            android:strokeColor="#FFFFFF"
            android:strokeWidth="45"
            android:strokeLineJoin="round"
            android:strokeLineCap="round"
            android:pathData="{re.sub(r",\s*", ",", p)}" />''' for p in paths)
vector = f'''<?xml version="1.0" encoding="utf-8"?>
<!-- Azalon3-DS peach silhouette (themed icon layer). Generated from the app icon SVG. -->
<vector xmlns:android="http://schemas.android.com/apk/res/android"
    android:width="108dp"
    android:height="108dp"
    android:viewportWidth="1024"
    android:viewportHeight="1024">
    <group
        android:translateX="{tx:.2f}"
        android:translateY="{ty:.2f}"
        android:scaleX="{ms:.5f}"
        android:scaleY="{ms:.5f}">
{body}
    </group>
</vector>
'''
open(os.path.join(REPO, RES, "drawable/ic_citra_monochrome.xml"), "w", encoding="utf-8",
     newline="\n").write(vector)
print("wrote drawable/ic_citra_monochrome.xml")

open(os.path.join(REPO, RES, "values/ic_launcher_background.xml"), "w", encoding="utf-8",
     newline="\n").write(f'''<?xml version="1.0" encoding="utf-8"?>
<resources>
    <color name="ic_launcher_background">{ANDROID_BG}</color>
</resources>
''')
print("wrote values/ic_launcher_background.xml")
