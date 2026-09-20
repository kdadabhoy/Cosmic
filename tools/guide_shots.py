"""guide_shots.py - turn the walkthrough driver's raw editor screenshots into guide images.

    py -3 tools/guide_shots.py <shots-dir> <result.json> <out-dir> [--max-width 1440] [--max-kb 600]

For every PNG in <shots-dir>: re-colour Dear ImGui's DebugLocateItem highlight (pure green,
0,255,0) to red, draw any extra rectangles listed in <shots-dir>/annotations.json
({"<name>": [[x0,y0,x1,y1], ...]} in raw-image pixels; the JSON's shot_rects are used when a
shot has no manual entry), downscale to --max-width, and re-encode until the file is under
--max-kb (palette-quantised as a last resort). Pillow only.
"""
import json
import os
import sys

from PIL import Image, ImageDraw

RED = (230, 40, 40)


def recolour_green(im):
    px = im.load()
    w, h = im.size
    n = 0
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y][:3]
            if g >= 250 and r <= 8 and b <= 8:
                px[x, y] = RED
                n += 1
    return n


def draw_rects(im, rects, width=3):
    d = ImageDraw.Draw(im)
    for r in rects:
        x0, y0, x1, y1 = r
        for i in range(width):
            d.rectangle([x0 - i, y0 - i, x1 + i, y1 + i], outline=RED)


def shrink_to_budget(im, path, max_kb):
    im.save(path, optimize=True)
    if os.path.getsize(path) <= max_kb * 1024:
        return os.path.getsize(path)
    q = im.convert('RGB').quantize(colors=256, method=Image.Quantize.MEDIANCUT)
    q.save(path, optimize=True)
    return os.path.getsize(path)


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 2
    shots, result, out = argv[1], argv[2], argv[3]
    max_width = 1440
    max_kb = 600
    if '--max-width' in argv:
        max_width = int(argv[argv.index('--max-width') + 1])
    if '--max-kb' in argv:
        max_kb = int(argv[argv.index('--max-kb') + 1])
    os.makedirs(out, exist_ok=True)
    rects = {}
    if os.path.exists(result):
        rects.update(json.load(open(result, encoding='utf-8')).get('shot_rects', {}))
    manual = {}
    ann = os.path.join(shots, 'annotations.json')
    if os.path.exists(ann):
        manual = json.load(open(ann, encoding='utf-8'))
    for name in sorted(os.listdir(shots)):
        if not name.lower().endswith('.png'):
            continue
        stem = name[:-4]
        im = Image.open(os.path.join(shots, name)).convert('RGB')
        greens = recolour_green(im)
        extra = manual.get(stem)
        if extra:
            draw_rects(im, extra)
        elif greens == 0 and stem in rects:
            draw_rects(im, [rects[stem]])
        if im.size[0] > max_width:
            scale = max_width / im.size[0]
            im = im.resize((max_width, int(im.size[1] * scale)), Image.LANCZOS)
        dst = os.path.join(out, name)
        size = shrink_to_budget(im, dst, max_kb)
        print(f'{name}: green px {greens}, manual rects {len(extra) if extra else 0}, {im.size[0]}x{im.size[1]}, {size // 1024} KB')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
