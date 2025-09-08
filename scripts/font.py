#!/usr/bin/env python3

# eg:
# python scripts/font.py /Users/alexe/Downloads/oldschool_pc_font_pack_v2.2_FULL/ttf\ -\ Px\ \(pixel\ outline\)/Px437_FMTowns_re_8x16.ttf --size 16 --offy 2 --start 32 -o scripts/font_256.png --no-aa && open scripts/font_256.png

import argparse
from PIL import Image, ImageDraw, ImageFont
import tqdm


W,H,NC,NR = 128,128,16,8
CW,CH = W//NC, H//NR

def max_bbox_for_size(ttf, size, chars):
    f = ImageFont.truetype(ttf, size=size)
    asc, dsc = f.getmetrics()
    w = max(int(f.getlength(c)) for c in chars)
    h = asc + dsc
    return w,h

def autoselect_size(ttf, chars, limit=(CW,CH)):
    lo,hi = 1, 256
    while lo<hi:
        mid = (lo+hi+1)//2
        w,h = max_bbox_for_size(ttf, mid, chars)
        if w<=limit[0] and h<=limit[1]: lo=mid
        else: hi=mid-1
    return lo

def render(ttf, out, size, start, end, offx, offy, aa, index):
    chars = [chr(c) for c in range(start, end+1)][:NC*NR]
    if size<=0: size = autoselect_size(ttf, chars)
    print(size)

    font = ImageFont.truetype(ttf, size=size, index=index)
    asc, dsc = font.getmetrics()
    baseline_y = int(0.75*size) + offy  # fixed baseline within each 16x32 cell

    img = Image.new("L", (W,H), 255)
    for i,c in tqdm.tqdm(enumerate(chars)):
        r,cx = divmod(i, NC)
        x0,y0 = cx*CW, r*CH
        cell = Image.new("L", (CW,CH), 255)
        cd = ImageDraw.Draw(cell)

        adv = int(round(font.getlength(c)))
        px = max(0, min(CW-adv, (CW-adv)//2 + offx))  # center horizontally (monospace)
        py = baseline_y - asc                         # align by baseline; may clip, that's fine

        cd.text((px, py), c, 0, font=font)
        img.paste(cell, (x0, y0))

    if not aa:
        img = img.point(lambda v: 0 if v<128 else 255, mode="1").convert("L")
    img.save(out)

if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("ttf")
    p.add_argument("--index", type=int, default=0, help="font face index (weight/style) in collection")
    p.add_argument("-o","--out", default="font_256.png")
    p.add_argument("--size", type=int, default=24, help="0=auto")
    p.add_argument("--start", type=int, default=32)
    p.add_argument("--end", type=int, default=127)
    p.add_argument("--offx", type=int, default=0)
    p.add_argument("--offy", type=int, default=2)
    aa = p.add_mutually_exclusive_group()
    aa.add_argument("--aa", dest="aa", action="store_true")
    aa.add_argument("--no-aa", dest="aa", action="store_false")
    p.set_defaults(aa=True)
    a = p.parse_args()
    try:
        for i in range(64):  # arbitrary upper bound
            f = ImageFont.truetype(a.ttf, size=12, index=i)
            print(f"{'*' if i==a.index else ' '} index {i}: {f.getname()}")
    except OSError:
        pass
    render(a.ttf, a.out, a.size, a.start, a.end, a.offx, a.offy, a.aa, a.index)
