from . api.block import NeighboringSet
from . import blocks


class Neighbors(object):
    GROUND_NEIGHBORING_SET = NeighboringSet(
        "ground", ["ground", "wood", "water"], {
            0: blocks.GROUND_0,
            1: blocks.GROUND_1,
            2: blocks.GROUND_2,
            3: blocks.GROUND_3,
            4: blocks.GROUND_4,
            5: blocks.GROUND_5,
            6: blocks.GROUND_6,
            7: blocks.GROUND_7,
            8: blocks.GROUND_8,
            9: blocks.GROUND_9,
            10: blocks.GROUND_10,
            11: blocks.GROUND_11,
            12: blocks.GROUND_12,
            13: blocks.GROUND_13,
            14: blocks.GROUND_14,
        }, blocks.GROUND_15,
        good_light_default=blocks.GROUND_15_SHADE_1,
        midrange_light_default=blocks.GROUND_15_SHADE_2,
        some_light_default=blocks.GROUND_15_SHADE_3)

    GRASS_NEIGHBORING_SET = NeighboringSet("grass", ["ground"], {
        3: blocks.GRASS_LEFT_TOP,
        6: blocks.GRASS_LEFT_BOTTOM,
        9: blocks.GRASS_LEFT_TOP,
        12: blocks.GRASS_LEFT_BOTTOM,
    }, blocks.GRASS)

    SPIKE_NEIGHBORING_SET = NeighboringSet("spike", ["ground"], {}, [
        blocks.SPIKE_0,
        blocks.SPIKE_1,
    ])

    TUBE_NEIGHBORING_SET = NeighboringSet("tube", ["tube", "power", "transport"], {
        1: blocks.TUBE_TOP_BOTTOM,
        2: blocks.TUBE_LEFT_RIGHT,
        3: blocks.TUBE_CORNER_TOP_RIGHT,
        4: blocks.TUBE_TOP_BOTTOM,
        5: blocks.TUBE_TOP_BOTTOM,
        6: blocks.TUBE_CORNER_BOTTOM_RIGHT,
        7: blocks.TUBE_4,
        8: blocks.TUBE_LEFT_RIGHT,
        9: blocks.TUBE_CORNER_TOP_LEFT,
        10: blocks.TUBE_LEFT_RIGHT,
        11: blocks.TUBE_4,
        12: blocks.TUBE_CORNER_BOTTOM_LEFT,
        13: blocks.TUBE_4,
        14: blocks.TUBE_4,
        15: blocks.TUBE_4,
    }, blocks.TUBE_0)

    FILLED_TUBE_NEIGHBORING_SET = NeighboringSet("tube", ["tube", "power", "transport"], {
        1: blocks.FILLED_TUBE_TOP_BOTTOM,
        2: blocks.FILLED_TUBE_LEFT_RIGHT,
        3: blocks.FILLED_TUBE_CORNER_TOP_RIGHT,
        4: blocks.FILLED_TUBE_TOP_BOTTOM,
        5: blocks.FILLED_TUBE_TOP_BOTTOM,
        6: blocks.FILLED_TUBE_CORNER_BOTTOM_RIGHT,
        7: blocks.FILLED_TUBE_4,
        8: blocks.FILLED_TUBE_LEFT_RIGHT,
        9: blocks.FILLED_TUBE_CORNER_TOP_LEFT,
        10: blocks.FILLED_TUBE_LEFT_RIGHT,
        11: blocks.FILLED_TUBE_4,
        12: blocks.FILLED_TUBE_CORNER_BOTTOM_LEFT,
        13: blocks.FILLED_TUBE_4,
        14: blocks.FILLED_TUBE_4,
        15: blocks.FILLED_TUBE_4,
    }, blocks.TUBE_0)

    WATER_SHADE_SURFACE = {
        2: blocks.WATER_SHADE_SURFACE,
        6: blocks.WATER_SHADE_SURFACE,
        8: blocks.WATER_SHADE_SURFACE,
        10: blocks.WATER_SHADE_SURFACE,
        12: blocks.WATER_SHADE_SURFACE,
        14: blocks.WATER_SHADE_SURFACE
    }

    WATER_NEIGHBORING_SET = NeighboringSet(
        "water", ["water", "solid", "semisolid"], {
            2: blocks.WATER_SURFACE,
            6: blocks.WATER_SURFACE,
            8: blocks.WATER_SURFACE,
            10: blocks.WATER_SURFACE,
            12: blocks.WATER_SURFACE,
            14: blocks.WATER_SURFACE
        },
        default_code=blocks.WATER,
        good_light_codes=WATER_SHADE_SURFACE,
        good_light_default=blocks.WATER_SHADE,
        midrange_light_codes=WATER_SHADE_SURFACE,
        midrange_light_default=blocks.WATER_SHADE,
        some_light_codes=WATER_SHADE_SURFACE,
        some_light_default=blocks.WATER_SHADE)

    BRIGHT_SKY_NEIGHBORING_SET = NeighboringSet("sky", ["sky"], {
        1: [blocks.BG_DAY_LEFT_RIGHT_1, blocks.BG_DAY_LEFT_RIGHT_2],
        2: [blocks.BG_DAY_LEFT_RIGHT_1, blocks.BG_DAY_LEFT_RIGHT_2],
        3: [blocks.BG_DAY_RIGHT_TOP_1, blocks.BG_DAY_RIGHT_TOP_2],
        8: [blocks.BG_DAY_LEFT_RIGHT_1, blocks.BG_DAY_LEFT_RIGHT_2],
        9: [blocks.BG_DAY_LEFT_TOP_1, blocks.BG_DAY_LEFT_TOP_2],
        10: [blocks.BG_DAY_LEFT_RIGHT_1, blocks.BG_DAY_LEFT_RIGHT_2],
        11: [blocks.BG_DAY_LEFT_RIGHT_1, blocks.BG_DAY_LEFT_RIGHT_2],
    }, blocks.BG_DAY_EMPTY)

    DARK_SKY_NEIGHBORING_SET = NeighboringSet("dark_sky", ["dark_sky"], {
        9: [blocks.BG_NIGHT_LEFT_TOP_1, blocks.BG_NIGHT_LEFT_TOP_2],
        3: [blocks.BG_NIGHT_RIGHT_TOP_1, blocks.BG_NIGHT_RIGHT_TOP_2],
        1: [blocks.BG_NIGHT_LEFT_RIGHT_1, blocks.BG_NIGHT_LEFT_RIGHT_2],
        2: [blocks.BG_NIGHT_LEFT_RIGHT_1, blocks.BG_NIGHT_LEFT_RIGHT_2],
        8: [blocks.BG_NIGHT_LEFT_RIGHT_1, blocks.BG_NIGHT_LEFT_RIGHT_2],
        11: [blocks.BG_NIGHT_LEFT_RIGHT_1, blocks.BG_NIGHT_LEFT_RIGHT_2],
        10: [blocks.BG_NIGHT_LEFT_RIGHT_1, blocks.BG_NIGHT_LEFT_RIGHT_2],
    }, [
        blocks.BG_NIGHT_EMPTY, blocks.BG_NIGHT_EMPTY, blocks.BG_NIGHT_EMPTY,
        blocks.BG_NIGHT_EMPTY, blocks.BG_NIGHT_EMPTY, blocks.BG_NIGHT_STAR
    ])

    SPECTRUM_NEIGHBORING_SET = NeighboringSet("spectrum", ["spectrum"], {
        0: blocks.SPECTRUM_NONE,
        1: blocks.SPECTRUM_NONE,
        2: blocks.SPECTRUM_NONE,
        3: blocks.SPECTRUM_CORNER_TOP_RIGHT,
        4: blocks.SPECTRUM_NONE,
        5: blocks.SPECTRUM_NONE,
        6: blocks.SPECTRUM_CORNER_BOTTOM_RIGHT,
        7: blocks.SPECTRUM_TOP_RIGHT_BOTTOM,
        8: blocks.SPECTRUM_NONE,
        9: blocks.SPECTRUM_CORNER_TOP_LEFT,
        10: blocks.SPECTRUM_NONE,
        11: blocks.SPECTRUM_TOP_LEFT_RIGHT,
        12: blocks.SPECTRUM_CORNER_BOTTOM_LEFT,
        13: blocks.SPECTRUM_TOP_LEFT_BOTTOM,
        14: blocks.SPECTRUM_LEFT_RIGHT_BOTTOM,
    }, [blocks.SPECTRUM_0, blocks.SPECTRUM_1])

    LADDERS_NEIGHBORING_SET = NeighboringSet("ladders", ["ladders"], {
        1: blocks.LADDER_BOTTOM,
        4: blocks.LADDER_TOP
    }, [blocks.LADDER_MIDDLE_0, blocks.LADDER_MIDDLE_1, blocks.LADDER_MIDDLE_2])
