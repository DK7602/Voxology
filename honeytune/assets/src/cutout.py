# Cuts art out of a "fake transparency" checkerboard: near-white / light-grey, unsaturated regions that
# touch the border (or are big) become transparent; edges get a soft alpha.
import sys, numpy as np
from PIL import Image, ImageFilter
from scipy import ndimage
def cutout(img, box=None, big=600, satMax=0.06, valMin=0.72):
    im = img.convert('RGB')
    if box: im = im.crop(box)
    a = np.asarray(im, dtype=np.float32) / 255
    mx, mn = a.max(2), a.min(2)
    sat = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1e-6), 0)
    checker = (sat < satMax) & (mx > valMin)
    lab, n = ndimage.label(checker)
    border = set(np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]]))) - {0}
    sizes = ndimage.sum(checker, lab, range(n + 1))
    bg = np.isin(lab, [i for i in range(1, n + 1) if i in border or sizes[i] > big])
    bg = ndimage.binary_opening(bg, iterations=1)
    alpha = (~bg).astype(np.float32)
    alpha = np.asarray(Image.fromarray((alpha * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.7)), dtype=np.float32) / 255
    alpha = np.clip((alpha - 0.15) / 0.7, 0, 1)
    rgba = np.dstack([np.asarray(im), (alpha * 255).astype(np.uint8)])
    out = Image.fromarray(rgba, 'RGBA')
    return out.crop(out.getbbox())
if __name__ == '__main__':
    src, dst = sys.argv[1], sys.argv[2]
    box = tuple(int(v) for v in sys.argv[3].split(',')) if len(sys.argv) > 3 else None
    o = cutout(Image.open(src), box)
    o.save(dst); print(dst, o.size)
