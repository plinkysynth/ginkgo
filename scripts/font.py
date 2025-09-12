#!/usr/bin/env python3
# defaults work for iosevka.ttc
# other examples:
# python scripts/font.py --font '/Users/alexe/Downloads/_decterm.ttf' --out assets/font_term.png --size 512 --baseline 480

import argparse, numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy.ndimage import distance_transform_edt as edt

def render_mask(ch, font, W, H, baseline):
    asc, _ = font.getmetrics()
    img = Image.new('L', (W, H), 0)
    ImageDraw.Draw(img).text((0, baseline - asc), ch, 255, font=font)
    return (np.asarray(img) >= 128)

def sdf_from_mask(mask):
    inside = edt(mask)
    outside = edt(~mask)
    return outside - inside  # >0 outside, <0 inside, 0 at edge

def resize_sdf(sdf, out_w, out_h):
    im = Image.fromarray(sdf.astype(np.float32), mode='F')
    while im.width >= out_w*2 and im.height >= out_h*2:
        im = im.resize((im.width//2, im.height//2), resample=Image.Resampling.BILINEAR)
    im = im.resize((out_w, out_h), resample=Image.Resampling.BILINEAR)
    return np.asarray(im).astype(np.float32)

def encode_u8(sdf, spread):
    return np.clip(128.0 + sdf * (-127.0 / float(spread)), 0, 255).astype(np.uint8)

def dump_font_variants(font):
    from fontTools.ttLib import TTCollection

    ttc = TTCollection(font)
    for i, ttfont in enumerate(ttc.fonts):
        name_table = ttfont["name"]
        family = name_table.getDebugName(1)   # Font Family
        subfam = name_table.getDebugName(2)   # Font Subfamily
        full   = name_table.getDebugName(4)   # Full name
        print(i, family, subfam)

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--font', default='/Users/alexe/Library/Fonts/Iosevka.ttc', help='.ttc/.ttf path')
    p.add_argument('--index', type=int, default=-1, help='subfamily index in TTC')
    p.add_argument('--size', type=int, default=480, help='font size (px)')
    p.add_argument('--baseline', type=int, default=400, help='baseline y in the 256x512 canvas')
    p.add_argument('--tmp_w', type=int, default=256)
    p.add_argument('--tmp_h', type=int, default=512)
    p.add_argument('--tile_w', type=int, default=32)
    p.add_argument('--tile_h', type=int, default=64)
    p.add_argument('--spread', type=float, default=64.0, help='SDF spread in output px')
    p.add_argument('--out', default='assets/font_sdf.png')
    args = p.parse_args()
    if not '.ttc' in args.font:
        args.index = 0

    if args.index == -1:
        dump_font_variants(args.font)
        exit()


    font = ImageFont.truetype(args.font, args.size, index=args.index)
    chars = list(range(32, 128))  # 96 glyphs to fill 16x6
    cols, rows = 16, 6
    assert len(chars) == cols * rows, "tile grid must match char count"

    atlas = Image.new('L', (cols * args.tile_w, rows * args.tile_h), 0)

    for i, cp in enumerate(chars):
        ch = chr(cp)
        mask = render_mask(ch, font, args.tmp_w, args.tmp_h, args.baseline)
        sdf = sdf_from_mask(mask)
        sdf_small = resize_sdf(sdf, args.tile_w, args.tile_h)
        tile_u8 = encode_u8(sdf_small, args.spread)
        tile_img = Image.fromarray(tile_u8, mode='L')
        x = (i % cols) * args.tile_w
        y = (i // cols) * args.tile_h
        atlas.paste(tile_img, (x, y))

    atlas.save(args.out)

if __name__ == '__main__':
    main()
