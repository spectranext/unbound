
from . art import art_bytes


class Image(object):
    INK_BLACK = 0x00
    INK_BLUE = 0x01
    INK_RED = 0x02
    INK_MAGENTA = 0x03
    INK_GREEN = 0x04
    INK_CYAN = 0x05
    INK_YELLOW = 0x06
    INK_WHITE = 0x07

    PAPER_BLACK = 0x00
    PAPER_BLUE = 0x08
    PAPER_RED = 0x10
    PAPER_MAGENTA = 0x18
    PAPER_GREEN = 0x20
    PAPER_CYAN = 0x28
    PAPER_YELLOW = 0x30
    PAPER_WHITE = 0x38

    BRIGHT = 0x40

    def __init__(self, source: bytes = None, w: int = 0, h: int = 0):
        if source:
            self.w = int(source[0]) * 8
            self.h = int(source[1]) * 8
            pixels = source[2:2 + (self.w * self.h // 8)]
            colors = source[2 + (self.w * self.h // 8):]
            self.pixels = [False] * (self.w * self.h)
            self.unpack_data(pixels)
            self.colors = [
                int(c)
                for c in colors
            ]
        else:
            self.w = w * 8
            self.h = h * 8
            self.pixels = [False] * (self.w * self.h)
            self.colors = [0] * (self.w * self.h // 64)

    def place_image(self, other: 'Image', x: int, y: int, start_x: int = 0, start_y: int = 0,
                    end_x: int = 0, end_y: int = 0):
        end_x = end_x or other.w
        end_y = end_y or other.h
        for _y in range(start_y, end_y):
            for _x in range(start_x, end_x):
                if other.pixel_at(_x, _y, 1):
                    self.set_pixel(x + _x, y + _y, True)
        xc = x // 8
        yc = y // 8
        for _y in range(start_y, end_y, 8):
            for _x in range(start_x, end_x, 8):
                self.set_color(xc + _x // 8, yc + _y // 8, other.color_at(_x // 8, _y // 8))

    def fill(self, x: int, y: int, w: int, h: int):
        for _y in range(0, h):
            for _x in range(0, w):
                self.set_pixel(x + _x, y + _y, True)

    def pixel_at(self, x, y, weight):
        return weight if self.pixels[x + y * self.w] else 0

    def color_at(self, x, y):
        return self.colors[x + y * self.w // 8]

    def set_pixel(self, x, y, v):
        self.pixels[x + y * self.w] = bool(v)
    
    def set_color(self, x: int, y: int, color: int):
        self.colors[x + y * self.w // 8] = color

    def unpack_data(self, data):
        i = 0
        for y in range(0, self.h, 8):
            for x in range(0, self.w, 8):
                for p in range(0, 8):
                    dd = data[i]
                    i += 1
                    yy = y + p
                    self.set_pixel(x, yy, bool(dd & 128))
                    self.set_pixel(x + 1, yy, bool(dd & 64))
                    self.set_pixel(x + 2, yy, bool(dd & 32))
                    self.set_pixel(x + 3, yy, bool(dd & 16))
                    self.set_pixel(x + 4, yy, bool(dd & 8))
                    self.set_pixel(x + 5, yy, bool(dd & 4))
                    self.set_pixel(x + 6, yy, bool(dd & 2))
                    self.set_pixel(x + 7, yy, bool(dd & 1))

    def pack_data(self):
        for y in range(0, self.h, 8):
            for x in range(0, self.w, 8):
                for p in range(0, 8):
                    yy = y + p
                    yield self.pixel_at(x, yy, 128) + \
                        self.pixel_at(x + 1, yy, 64) + \
                        self.pixel_at(x + 2, yy, 32) + \
                        self.pixel_at(x + 3, yy, 16) + \
                        self.pixel_at(x + 4, yy, 8) + \
                        self.pixel_at(x + 5, yy, 4) + \
                        self.pixel_at(x + 6, yy, 2) + \
                        self.pixel_at(x + 7, yy, 1)

    def bake(self) -> bytes:
        data = bytes(self.pack_data())

        return art_bytes(self.w // 8, self.h // 8, data, bytes(self.colors))
