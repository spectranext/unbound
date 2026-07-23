
import argparse
import os
import png
import csv

from .. items import Item
from .. map import ServerMap
from .. bases import BaseItem
from .. contract.list import CONTRACTS

from jinja2 import Environment, FileSystemLoader

parser = argparse.ArgumentParser(description='Generate documentation for the server')
parser.add_argument('--output', type=str, required=True, help='File generate the output into')
parser.add_argument('--template', type=str, required=True, help='Template to process')
parser.add_argument('--work-dir', type=str, required=True, help='Template working directory')
parser.add_argument('--images', type=str, help='Images output')

args = parser.parse_args()
output = args.output
template = args.template
work_dir = args.work_dir
images_dir = args.images
script_dir = os.path.dirname(os.path.realpath(__file__))

tile_images = [()]


def create_folders():
    if images_dir:
        if not os.path.isdir(images_dir):
            os.mkdir(images_dir)
        if not os.path.isdir(os.path.join(images_dir, "images")):
            os.mkdir(os.path.join(images_dir, "images"))


def bake_image(width, height, images, filename, border):
    img = []
    empty_row = [0] * (width * 16 * 3 + border * 2 * 3)
    for x in range(0, height * 16 + border * 2):
        img.append(empty_row.copy())

    for y, row in enumerate(images):
        for x, cell in enumerate(row):
            if cell == 0:
                continue
            tile_image = tile_images[cell]
            for y1 in range(0, 16):
                for x1 in range(0, 16):
                    rr = img[y * 16 + y1 + border]
                    i = x * 16 + x1
                    rr[i * 3 + border * 3] = tile_image[y1][x1 * 3]
                    rr[i * 3 + 1 + border * 3] = tile_image[y1][x1 * 3 + 1]
                    rr[i * 3 + 2 + border * 3] = tile_image[y1][x1 * 3 + 2]

    img_width = width * 16 + border * 2
    img_height = height * 16 + border * 2

    with open(os.path.join(images_dir, "images", filename), 'wb') as f:
        w = png.Writer(img_width, img_height, greyscale=False)
        w.write(f, [tuple(x) for x in img])
    return os.path.join("images", filename), img_width, img_height


def generate_icons():
    if not images_dir:
        return
    colors = [(0, 0, 0),
        (0, 0, 206),
        (206, 0, 0),
        (206, 0, 206),
        (0, 206, 0),
        (0, 206, 206),
        (206, 206, 0),
        (206, 206, 206)]

    colors_bright = [(0, 0, 0),
        (0, 0, 255),
        (255, 0, 0),
        (255, 0, 255),
        (0, 255, 0),
        (0, 255, 255),
        (255, 255, 0),
        (255, 255, 255)]

    with open(os.path.join(script_dir, "icons"), "r") as f:
        icons = bytes.fromhex(f.readline().split('=')[1])
        icon_colors = bytes.fromhex(f.readline().split('=')[1])

    for index in range(1, len(icon_colors)):
        color = icon_colors[index]
        pixels = icons[index * 8:(index+1) * 8]

        foreground = color & 0b00000111
        background = (color & 0b00111000) >> 3
        bright = bool(color & 0b01000000)

        with open(os.path.join(images_dir, "images", "{0}.png".format(str(index))), 'wb') as f:
            w = png.Writer(16, 16, greyscale=False)
            img = []
            for row in pixels:
                r = ()
                for b in range(0, 8):
                    bit_set = row & (1 << (7 - b))
                    color_set = colors_bright if bright else colors
                    color = color_set[foreground if bit_set else background]
                    # 2x
                    r += color
                    r += color
                # 2x
                img.append(r)
                img.append(r)
            tile_images.append(img)
            w.write(f, img)

        with open(os.path.join(images_dir, "images", "{0}_ext.png".format(str(index))), 'wb') as f:
            w = png.Writer(20, 20, greyscale=False)
            empty_row = ()
            for x in range(0, 20):
                empty_row += (0, 0, 0)
            img = [empty_row, empty_row]
            for row in pixels:
                r = ()
                r += (0, 0, 0)
                r += (0, 0, 0)
                for b in range(0, 8):
                    bit_set = row & (1 << b)
                    color_set = colors_bright if bright else colors
                    color = color_set[foreground if bit_set else background]
                    # 2x
                    r += color
                    r += color
                r += (0, 0, 0)
                r += (0, 0, 0)
                # 2x
                img.append(r)
                img.append(r)
            img.append(empty_row)
            img.append(empty_row)
            w.write(f, img)

    for key, b in Item.ITEMS.items():
        if not isinstance(b, BaseItem):
            continue
        bake_image(b.width, b.height, b.icons_set(), "base_{0}.png".format(b.identity), 8)

    bake_image(len(tile_images), 1, [[n for n in range(0, len(tile_images))]], "tileset.png", 0)


def generate_docs():
    env = Environment(loader=FileSystemLoader(work_dir))
    env.filters['wrap_images'] = wrap_images
    env.globals['csv_tile_image'] = csv_tile_image

    with open(os.path.join(output), "w") as f:
        t = env.get_template(template)
        f.write(t.render(items=Item.ITEMS, contracts=CONTRACTS))


def create_api():
    instance = ServerMap()


def wrap_images(text):
    if not isinstance(text, str):
        return text

    import re

    def callback(match):
        numbers = match.group(1).split(",")
        alt = numbers[0]
        width = int(numbers[1])
        height = int(numbers[2])
        numbers = list(map(lambda x: int(x.strip()), numbers[3:]))
        r_output = []

        for y in range(0, height):
            r_output.append(numbers[y * width:(y+1) * width])

        fname = "".join([str(x) for x in numbers]) + ".png"

        fname, w, h = bake_image(width, height, r_output, fname, 8)

        return "<img src=\"{0}\" width=\"{1}\" alt=\"{2}\">".format(fname, w, alt)

    return re.sub(r"tile_image\((.+)\)", callback, text, flags=re.MULTILINE)


def csv_tile_image(name, filename, alt):
    with open(os.path.join(script_dir, filename), 'r') as csvfile:
        spamreader = list(csv.reader(csvfile, delimiter=','))

    res = [
        [
            int(v)
            for v in row
        ]
        for row in spamreader
    ]

    height = len(res)
    width = len(res[0])

    fname = "{0}.png".format(name)

    fname, w, h = bake_image(width, height, res, fname, 8)

    return "<img src=\"{0}\" width=\"{1}\" alt=\"{2}\">".format(fname, w, alt)


create_api()
create_folders()
generate_icons()
generate_docs()
