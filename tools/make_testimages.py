#!/usr/bin/env python3
"""
make_testimages.py - generate WebP test pictures for webp.datatype.

Usage: make_testimages.py <output_dir>

Needs Pillow with WebP support. Each file exercises one code path of
src/webpclass.c; see the TESTS table printed at the end.
"""
import os
import sys

from PIL import Image, ImageDraw


def base_picture(w, h, alpha=False):
    """Colour gradients + shapes + text: easy to judge by eye on Amiga."""
    img = Image.new("RGBA" if alpha else "RGB", (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            r = x * 255 // max(1, w - 1)
            g = y * 255 // max(1, h - 1)
            b = 255 - (r + g) // 2
            if alpha:
                # alpha: opaque on the left, transparent on the right
                px[x, y] = (r, g, b, 255 - r)
            else:
                px[x, y] = (r, g, b)
    d = ImageDraw.Draw(img)
    full = (255, 255, 255, 255) if alpha else (255, 255, 255)
    black = (0, 0, 0, 255) if alpha else (0, 0, 0)
    d.rectangle([w // 10, h // 10, w // 3, h // 3], outline=full, width=3)
    d.ellipse([w // 2, h // 2, w - w // 10, h - h // 10], fill=black)
    d.text((w // 10, h // 2), "webp.datatype %dx%d" % (w, h), fill=full)
    return img


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)

    def save(name, img, **kw):
        path = os.path.join(out, name)
        img.save(path, "WEBP", **kw)
        return path

    tests = []

    img = base_picture(320, 256)
    save("01_lossy.webp", img, quality=80)
    tests.append(("01_lossy.webp", "VP8 lossy, opaque -> depth 24, masking 0"))

    save("02_lossless.webp", img, lossless=True)
    tests.append(("02_lossless.webp", "VP8L lossless, opaque -> depth 24"))

    imga = base_picture(320, 256, alpha=True)
    save("03_lossy_alpha.webp", imga, quality=80)
    tests.append(("03_lossy_alpha.webp", "VP8 + ALPH chunk -> depth 32, masking 4 (mskHasAlpha)"))

    save("04_lossless_alpha.webp", imga, lossless=True)
    tests.append(("04_lossless_alpha.webp", "VP8L with alpha -> depth 32, masking 4"))

    save("05_odd_size.webp", base_picture(97, 61), quality=80)
    tests.append(("05_odd_size.webp", "odd width/height: stride & chroma rounding"))

    save("06_tiny.webp", Image.new("RGB", (1, 1), (255, 0, 0)), lossless=True)
    tests.append(("06_tiny.webp", "1x1 red pixel"))

    save("07_large.webp", base_picture(1920, 1080), quality=85)
    tests.append(("07_large.webp", "1920x1080: memory use + decode time on 68020"))

    # animation: 3 full-size frames, first frame is red-tinted
    frames = []
    for i, col in enumerate([(255, 0, 0), (0, 255, 0), (0, 0, 255)]):
        f = base_picture(200, 150)
        ImageDraw.Draw(f).rectangle([0, 0, 199, 20], fill=col)
        ImageDraw.Draw(f).text((5, 5), "frame %d" % (i + 1), fill=(0, 0, 0))
        frames.append(f)
    save("08_animated.webp", frames[0], save_all=True,
         append_images=frames[1:], duration=300, loop=0, lossless=True)
    tests.append(("08_animated.webp", "animation: first frame shown (red bar, 'frame 1')"))

    # error paths
    data = open(os.path.join(out, "01_lossy.webp"), "rb").read()
    open(os.path.join(out, "09_truncated.webp"), "wb").write(data[: len(data) // 2])
    tests.append(("09_truncated.webp", "must FAIL cleanly (not enough data), no crash"))

    bad = bytearray(data)
    for i in range(40, min(len(bad), 400)):
        bad[i] ^= 0x5A
    open(os.path.join(out, "10_corrupt.webp"), "wb").write(bytes(bad))
    tests.append(("10_corrupt.webp", "must FAIL cleanly (invalid data), no crash"))

    # host-side check: everything that should decode does
    for name, _ in tests:
        if name.startswith(("09", "10")):
            continue
        with Image.open(os.path.join(out, name)) as im:
            im.load()

    print("TESTS (in %s):" % out)
    for name, what in tests:
        print("  %-24s %s" % (name, what))


if __name__ == "__main__":
    main()
