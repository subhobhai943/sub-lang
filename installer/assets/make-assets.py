#!/usr/bin/env python3
"""Generate the installer artwork from the two source logos in docs/.

Everything an installer shows is derived here rather than drawn by hand, so
the Windows dialogs, the Linux icon theme and the macOS package background
cannot drift apart from the logo in the README. Re-run after changing a
source logo:

    python3 installer/assets/make-assets.py

The two sources differ in an important way:

  docs/sub-lang-logo.png       the dark rounded card. Legible on any
                               background, so it is what every icon uses.
  docs/sub-lang-logo-mark.png  the bare dragon, transparent, with the
                               wordmark in white. Only usable on a dark
                               background -- on white the "SUB" disappears.
"""

import os
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

CARD = os.path.join(ROOT, "docs", "sub-lang-logo.png")
MARK = os.path.join(ROOT, "docs", "sub-lang-logo-mark.png")

INK = (13, 17, 23)        # #0d1117, the card background
ACCENT = (8, 180, 251)    # #08b4fb, the dragon
PAPER = (255, 255, 255)   # the MSI dialog background, which we must match


def load(path):
    return Image.open(path).convert("RGBA")


def fit(img, box):
    """Scale to fit inside box, preserving aspect."""
    w, h = img.size
    s = min(box[0] / w, box[1] / h)
    return img.resize((max(1, round(w * s)), max(1, round(h * s))), Image.LANCZOS)


def paste(dst, src, xy):
    dst.paste(src, xy, src)


# ── Windows: the two bitmaps WixUI draws ────────────────────────────────
#
# The sizes are not ours to choose: WixUI lays its controls out in dialog
# units against bitmaps of exactly these pixel dimensions, and anything else
# is stretched. 24-bit BMP, because a 32-bit one with an alpha channel is
# composited inconsistently across Windows versions.

def windows_banner(out):
    """493x58, across the top of every dialog after the first.

    WixUI draws the dialog title over this in black text starting at the
    left, so the left two thirds have to stay light and empty. The artwork
    goes on the right, which is where the stock WiX banner puts it too.
    """
    img = Image.new("RGB", (493, 58), PAPER)
    card = fit(load(CARD), (42, 42))
    paste(img, card, (493 - card.width - 14, (58 - card.height) // 2))
    # A hairline in the accent colour so the banner reads as part of a
    # designed installer rather than a blank strip.
    for y in range(56, 58):
        for x in range(493):
            img.putpixel((x, y), ACCENT)
    img.save(out)
    return out


def windows_dialog(out):
    """493x312, the background of the Welcome and Finish dialogs.

    WixUI puts the welcome text at 135 dialog units of 370, which lands at
    x=180px, and draws it transparently in black. So: dark brand panel on
    the left up to x=164, white from there on. Text never crosses onto the
    dark side.
    """
    img = Image.new("RGB", (493, 312), PAPER)
    panel = Image.new("RGB", (164, 312), INK)
    img.paste(panel, (0, 0))
    for x in range(164, 167):
        for y in range(312):
            img.putpixel((x, y), ACCENT)

    mark = fit(load(MARK), (128, 128))
    # The mark is transparent, so flatten it onto the panel colour rather
    # than onto white.
    bed = Image.new("RGBA", mark.size, INK + (255,))
    bed.alpha_composite(mark)
    img.paste(bed.convert("RGB"), ((164 - mark.width) // 2, 92))
    img.save(out)
    return out


def windows_ico(out):
    """The Add/Remove Programs icon, the Start Menu shortcut, and -- once
    the MSI stopped pointing at an executable that has no icon resource --
    the icon on every .sb file."""
    card = load(CARD)
    sizes = [(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (24, 24), (16, 16)]
    card.save(out, format="ICO", sizes=sizes)
    return out


# ── Linux: the hicolor icon theme ───────────────────────────────────────

def linux_icons(outdir):
    card = load(CARD)
    made = []
    for s in (16, 22, 24, 32, 48, 64, 128, 256, 512):
        p = os.path.join(outdir, "sub-lang-%d.png" % s)
        card.resize((s, s), Image.LANCZOS).save(p)
        made.append(p)
    return made


# ── macOS: the installer background ─────────────────────────────────────

def macos_background(out):
    """productbuild scales this proportionally into the left pane of the
    installer window. Transparent, so it sits on the system background
    rather than punching a white rectangle into it."""
    img = Image.new("RGBA", (620, 418), (0, 0, 0, 0))
    card = fit(load(CARD), (200, 200))
    paste(img, card, (34, 418 - card.height - 40))
    img.save(out)
    return out


def main():
    made = []
    made.append(windows_banner(os.path.join(ROOT, "installer", "windows", "banner.bmp")))
    made.append(windows_dialog(os.path.join(ROOT, "installer", "windows", "dialog.bmp")))
    made.append(windows_ico(os.path.join(ROOT, "installer", "windows", "sub-lang.ico")))
    made += linux_icons(os.path.join(ROOT, "installer", "linux", "icons"))
    made.append(macos_background(os.path.join(ROOT, "installer", "macos", "background.png")))
    for p in made:
        print(os.path.relpath(p, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
