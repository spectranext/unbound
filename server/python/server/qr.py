import qrcode
from . imagegen import Image


def qr_image(payload: str):
    qr = qrcode.make(payload)
    scale = 1
    w = len(qr.modules[0])
    h = len(qr.modules)
    off_x = 0
    off_y = 0
    if w % 8 != 0:
        old_w = w
        w = ((w // 8) + 1) * 8
        off_x = (w - old_w) // 2
    if h % 8 != 0:
        old_h = h
        h = ((h // 8) + 1) * 8
        off_y = (h - old_h) // 2

    if w <= 60:
        scale = 2

    im = Image(w=(w // 8) * scale, h=(h // 8) * scale)

    for y in range(0, (h // 8) * scale):
        for x in range(0, (w // 8) * scale):
            im.set_color(x, y, Image.PAPER_WHITE | Image.INK_BLACK | Image.BRIGHT)

    for y, row in enumerate(qr.modules):
        for x, val in enumerate(row):
            for sy in range(0, scale):
                for sx in range(0, scale):
                    im.set_pixel((x + off_x) * scale + sx, (y + off_y) * scale + sy, val)

    return im
