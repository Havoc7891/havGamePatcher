# SPDX-License-Identifier: MIT

"""Draw the original havGamePatcher cartridge-repair icon with Pillow.

Run: python scripts/generateIcon.py  (requires Pillow)
A cartridge bears a prominent red ball-top joystick, with a silver wrench
alongside it. All artwork is authored geometric drawing. No reference image
is opened or used as pixel data.

The master uses a 256px RGBA canvas and deliberate two-pixel cells. Native
16, 24 and 32px drawings simplify the same composition. Other sizes use
nearest-neighbor scaling. All edges and transparent corners remain crisp.
"""

from pathlib import Path

from PIL import Image, ImageDraw

RESOURCES = Path(__file__).resolve().parents[1] / "resources"
SIZES = (16, 24, 32, 48, 64, 128, 256)
INK = "#01050B"
NAVY = "#031323"
SILVER = "#D4D6DD"
STEEL = "#777C8B"
WHITE = "#F6F6F9"


class PixelCanvas:
    """Draw on a 256px RGBA canvas in crisp two-pixel cells.

    Design coordinates span 64 units. Half-unit coordinates permit small
    metallic bevels while the main shapes retain deliberate stepped edges.
    All temporary masks are newly drawn Pillow primitives.
    """

    def __init__(self):
        self.image = Image.new("RGBA", (256, 256))
        self.draw = ImageDraw.Draw(self.image)

    def primitive(self, kind, xy, color, **kwargs):
        if isinstance(xy[0], (tuple, list)):
            xy = [(round(x * 2), round(y * 2)) for x, y in xy]
        else:
            xy = tuple(round(v * 2) for v in xy)

        kwargs = {key: round(value * 2) for key, value in kwargs.items()}
        mask = Image.new("1", (128, 128))
        getattr(ImageDraw.Draw(mask), kind)(xy, fill=1, **kwargs)
        pixels = mask.load()

        for y in range(128):
            x = 0

            while x < 128:
                if not pixels[x, y]:
                    x += 1
                    continue

                start = x

                while x < 128 and pixels[x, y]:
                    x += 1

                self.draw.rectangle((start * 2, y * 2, x * 2 - 1, y * 2 + 1), fill=color)

    def rect(self, xy, color):
        self.primitive("rectangle", xy, color)

    def poly(self, xy, color):
        self.primitive("polygon", xy, color)

    def line(self, xy, color, width=0.5):
        self.primitive("line", xy, color, width=width)

    def rounded(self, xy, radius, color):
        self.primitive("rounded_rectangle", xy, color, radius=radius)

    def ellipse(self, xy, color):
        self.primitive("ellipse", xy, color)


def draw_master():
    p = PixelCanvas()

    # Quiet, stepped silver frame around a deep-blue field.
    p.rounded((1, 1, 62, 62), 9, "#566170")
    p.rounded((1, 1, 61.5, 61.5), 8.5, "#D4D6DD")
    p.rounded((1.5, 1.5, 61.5, 61.5), 8.5, "#8A91A1")
    p.rounded((2.5, 2.5, 60.5, 60.5), 7.5, INK)
    p.rounded((3.5, 3.5, 59.5, 59.5), 6.5, NAVY)
    p.line([(10, 1), (52, 1)], "#E5E5EA")
    p.line([(1, 11), (1, 50)], "#B3B6C1")
    p.line([(10, 61.5), (52, 61.5)], "#5B606B")

    # Cartridge shell lit from the upper left, with flat pixel-aligned facets.
    p.poly(
        [(13, 8), (37, 8), (42, 13), (46, 17), (46, 52), (41, 57), (12, 57), (8, 53), (8, 12)], INK
    )
    p.poly(
        [
            (13, 9),
            (36.5, 9),
            (41, 13.5),
            (44.5, 17),
            (44.5, 51.5),
            (40, 56),
            (12.5, 56),
            (9, 52.5),
            (9, 13),
        ],
        "#69788D",
    )
    p.poly(
        [
            (13, 10),
            (35.5, 10),
            (40, 14),
            (43, 17),
            (43, 51),
            (39, 54.5),
            (13, 54.5),
            (10.5, 51.5),
            (10.5, 13),
        ],
        "#46546A",
    )

    # Broad top bevel and narrow left bevel catch the light.
    p.poly([(13, 9.5), (36, 9.5), (39, 12.5), (13, 12.5), (10, 15), (10, 13)], "#8795A8")
    p.poly([(13.5, 11), (35.5, 11), (37.5, 13), (13.5, 13)], "#62738B")
    p.poly([(10, 14), (12, 12.5), (12.5, 51), (14.5, 53.5), (12.5, 53.5), (10, 51.5)], "#5C7088")
    p.line([(13, 9.5), (35.5, 9.5), (38.5, 12.5)], "#C2CAD5", 0.5)
    p.line([(9.5, 14), (9.5, 50)], "#A3B2C5", 0.5)

    # The right side and lower lip recede into shadow.
    p.poly(
        [(39, 13), (44.5, 17.5), (44.5, 51.5), (40, 56), (38.5, 53), (41, 50), (41, 17)], "#202C3D"
    )
    p.poly([(42.5, 18), (44.5, 18), (44.5, 51.5), (40, 56), (39.5, 54), (42.5, 50.5)], "#101B2A")
    p.line([(44.5, 18), (44.5, 51.5), (40, 56)], "#46546A", 0.5)
    p.poly([(11, 51.5), (14, 54), (38.5, 54), (40, 56), (12.5, 56), (10, 53)], "#263448")
    p.line([(13, 54.5), (38, 54.5)], "#52647C", 0.5)
    p.line([(13, 56), (40, 56)], "#101B2A", 0.5)

    # The inset label has a dark upper/left recess and a lit lower rim.
    p.rect((13.5, 14.5, 39, 46), INK)
    p.rect((14.5, 15.5, 38, 45), "#101E30")
    p.rect((15.5, 16.5, 37.5, 25), "#13243A")
    p.rect((15.5, 35, 37.5, 45), "#0B1728")
    p.line([(14.5, 44.5), (14.5, 15.5), (38, 15.5)], "#060D18", 0.5)
    p.line([(14, 46.5), (38.5, 46.5)], "#8291A6", 0.5)
    p.line([(39, 16.5), (39, 45.5)], "#586A83", 0.5)

    # Sunset bars belong to the cartridge label, not a background swoosh.
    p.rect((15.5, 17, 36.5, 19.5), "#FFBB00")
    p.rect((15.5, 17, 36.5, 17.5), "#FFD031")
    p.rect((15.5, 21, 33.5, 23.5), "#FF8C00")
    p.rect((15.5, 25, 30, 27.5), "#FF4510")

    # Side-profile joystick: level deck, shallow top and a shaded front face.
    p.poly(
        [
            (17, 36.5),
            (33, 36.5),
            (35, 38.5),
            (35, 43),
            (33.5, 44.5),
            (16.5, 44.5),
            (15, 43),
            (15, 38.5),
        ],
        INK,
    )
    p.poly([(17, 37.5), (33, 37.5), (34, 38.5), (34, 40), (16, 40), (16, 38.5)], "#A6ADB9")
    p.rect((17, 38.5, 33, 39.5), "#344055")
    p.poly([(16, 40), (34, 40), (34, 42.5), (33, 43.5), (17, 43.5), (16, 42.5)], "#18222F")
    p.rect((17, 40.5, 33, 41.5), "#344055")
    p.line([(16, 40), (34, 40)], "#D4D6DD", 0.5)
    p.line([(16, 40.5), (16, 42.5), (17, 43.5)], "#566170", 0.5)
    p.line([(17.5, 43.5), (32.5, 43.5)], "#263448", 0.5)

    # Inset the fire button on the deck, leaving room beside the shaft.
    p.poly([(28.5, 37), (31.5, 37), (32.5, 38), (32.5, 39.5), (27.5, 39.5), (27.5, 38)], INK)
    p.rect((28.5, 38, 31.5, 39), "#A9000C")
    p.rect((28.5, 37.5, 31, 38.5), "#FF2018")
    p.rect((28.5, 37.5, 29.5, 37.5), "#FFF4ED")
    p.ellipse((18.5, 35.5, 26.5, 39), INK)
    p.line([(19.5, 37.5), (21, 38.5), (24, 38.5), (25.5, 37.5)], "#778396", 0.5)

    # Move the short shaft slightly left to open space between the controls.
    p.rect((21, 31, 24, 37.5), INK)
    p.rect((21.5, 31, 23.5, 37), "#A6ADB9")
    p.rect((21.5, 31, 22, 37), "#F6F6F9")
    p.rect((23, 31, 23.5, 37), "#566170")
    p.line([(20, 37.5), (21.5, 38), (24, 38), (25.5, 37)], INK, 0.5)

    # Concentric pixel circles give both sides the same contour about the shaft.
    p.ellipse((18, 23, 27, 32), INK)

    ball = PixelCanvas()
    ball.ellipse((18.5, 23.5, 26.5, 31.5), "#FF2018")
    ball_alpha = ball.image.getchannel("A")

    # Clip the directional shading to the symmetric red silhouette.
    ball.poly(
        [
            (24.5, 24),
            (27, 24),
            (27, 32),
            (21.5, 32),
            (21.5, 30.5),
            (23.5, 30),
            (25, 28.5),
            (25, 26.5),
        ],
        "#A9000C",
    )
    ball.poly(
        [(20.5, 24), (23, 24), (23, 24.5), (21, 24.5), (20, 25.5), (19, 26.5), (19, 25.5)],
        "#FF5140",
    )
    ball.poly(
        [(20.5, 25), (22, 25), (22, 26), (21, 26), (21, 27), (19.5, 27), (19.5, 26), (20.5, 26)],
        "#FFF4ED",
    )

    ball.image.putalpha(ball_alpha)
    p.image.alpha_composite(ball.image)

    # Recessed connector makes the cartridge recognizable without lettering.
    p.rect((14, 48, 34, 54.5), INK)
    p.rect((15, 48.5, 33, 50), "#1B2636")

    for x in (16, 20, 24, 28, 32):
        p.rect((x, 50, x + 1.5, 53.5), "#BE711D")
        p.rect((x, 50, x + 1, 52.5), "#FFD031")

    p.line([(14, 48), (34, 48)], "#778396", 0.5)

    # Wrench rises along the right edge, with a dark gap beside the joystick.
    p.poly(
        [
            (46, 17),
            (50, 17),
            (54, 19),
            (57, 23),
            (57, 28.5),
            (55, 32),
            (51.5, 34.5),
            (42, 51.5),
            (40, 54.5),
            (37, 56),
            (33.5, 54.5),
            (32, 51.5),
            (33, 48.5),
            (42.5, 32.5),
            (40.5, 30),
            (39.5, 27),
            (40, 23),
            (42, 21.5),
            (44.5, 24),
            (47, 25),
            (49, 23),
            (48, 21),
            (45.5, 19.5),
        ],
        INK,
    )
    p.poly(
        [
            (47, 18),
            (50, 18),
            (53.5, 20),
            (56, 23.5),
            (56, 28),
            (54, 31),
            (50.5, 33.5),
            (41, 51.5),
            (39.5, 53.5),
            (37, 55),
            (34.5, 53.5),
            (33, 51),
            (34, 48.5),
            (44, 32.5),
            (41.5, 29.5),
            (40.5, 27),
            (41, 24),
            (42, 23),
            (44.5, 25.5),
            (47.5, 26),
            (50, 23.5),
            (49, 20.5),
            (47, 19),
        ],
        "#777C8B",
    )
    p.poly(
        [
            (47, 18),
            (50, 18),
            (53, 20),
            (55, 23),
            (55, 27.5),
            (53, 30.5),
            (49.5, 32.5),
            (40, 50.5),
            (38.5, 52.5),
            (36.5, 53.5),
            (34.5, 52),
            (34, 50),
            (44.5, 32),
            (42.5, 29),
            (41.5, 27),
            (42, 24),
            (44, 26),
            (47.5, 27),
            (51, 24),
            (50, 20.5),
            (47, 19),
        ],
        "#D4D6DD",
    )
    p.poly(
        [
            (52, 21),
            (54, 23.5),
            (54, 27),
            (52, 29.5),
            (49, 31.5),
            (47, 31),
            (44, 29),
            (43, 27),
            (47.5, 28),
            (52, 24.5),
        ],
        "#F0F0F5",
    )
    p.line([(47, 18), (50, 18), (53, 20), (55, 23)], "#F6F6F9", 0.5)
    p.line([(42, 24), (41.5, 27), (44.5, 32), (34, 50), (34.5, 52), (36.5, 53.5)], "#F6F6F9", 1)
    p.line([(49.5, 33), (40, 50.5)], "#A6A9B3", 0.5)

    # Long engraved handle channel emphasizes the wrench silhouette.
    p.poly([(46, 33), (48, 34), (39.5, 49), (37.5, 50), (36.5, 48.5)], "#F6F6F9")
    p.poly([(46, 34), (47, 34.5), (39, 48.5), (38, 49), (37.5, 48)], "#777C8B")
    p.line([(46, 35), (38.5, 48)], "#A6A9B3", 0.5)
    p.poly(
        [(36, 50), (38.5, 50), (39.5, 51), (39, 52.5), (37.5, 53.5), (35.5, 52.5), (35, 51)],
        "#F6F6F9",
    )
    p.poly([(36.5, 50.5), (38, 50.5), (38.5, 51.5), (37.5, 52.5), (36, 52), (35.5, 51.5)], INK)

    return p.image


def draw_small(size):
    if size not in (16, 24, 32):
        raise ValueError("Native small artwork supports 16, 24 and 32 pixels")

    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    r = d.rectangle
    p = d.polygon
    line = d.line

    # Each frame is drawn at its target size without resampling or antialiasing.
    d.rounded_rectangle((0, 0, size - 1, size - 1), radius=max(2, size // 7), fill=STEEL)
    d.rounded_rectangle((1, 1, size - 2, size - 2), radius=max(1, size // 7 - 1), fill=NAVY)
    line([(size // 5, 0), (size - size // 5 - 1, 0)], fill="#B3B6C1")

    if size == 16:
        # Stepped cartridge shell and uninterrupted navy joystick label.
        p([(3, 2), (8, 2), (10, 4), (10, 12), (9, 14), (3, 14), (2, 13), (2, 3)], fill="#747E91")
        p(
            [(8, 2), (10, 4), (10, 12), (9, 14), (3, 14), (2, 13), (8, 13), (9, 12), (9, 4)],
            fill="#202C3D",
        )
        p([(3, 3), (8, 3), (9, 4), (9, 12), (8, 13), (3, 13)], fill="#46546A")
        line([(3, 2), (7, 2)], fill="#B3B6C1")
        line([(2, 3), (2, 11)], fill="#9EA7B7")

        r((3, 4, 8, 11), fill=INK)
        r((4, 4, 8, 4), fill="#FFBB00")
        r((4, 5, 7, 5), fill="#FF8C00")
        r((4, 6, 4, 7), fill="#FF4510")

        # Tiny ball sits left of the button with a full dark pixel between.
        p([(4, 5), (5, 5), (6, 6), (6, 7), (5, 8), (4, 8), (3, 7), (3, 6)], fill=INK)
        r((4, 6, 5, 7), fill="#FF2018")
        r((5, 7, 5, 7), fill="#A9000C")
        r((4, 6, 4, 6), fill="#FFF4ED")
        r((5, 8, 5, 9), fill=WHITE)

        # Level side-profile base with one raised red button above the top edge.
        r((4, 10, 8, 10), fill=SILVER)
        r((4, 11, 8, 11), fill="#344055")
        r((7, 9, 7, 9), fill="#FF2018")
        r((3, 12, 8, 13), fill=INK)

        for x in (4, 6, 8):
            r((x, 13, x, 13), fill="#FFD031")

        # Large open jaw and solid diagonal shaft. Omit the tiny handle hole.
        p(
            [
                (11, 4),
                (12, 4),
                (14, 6),
                (14, 8),
                (13, 9),
                (10, 14),
                (8, 14),
                (7, 13),
                (8, 11),
                (10, 8),
                (9, 7),
                (9, 6),
                (10, 5),
                (11, 6),
                (12, 6),
                (11, 5),
            ],
            fill=INK,
        )
        p(
            [
                (12, 5),
                (13, 6),
                (13, 8),
                (12, 9),
                (10, 12),
                (9, 13),
                (8, 13),
                (8, 12),
                (11, 8),
                (10, 7),
                (10, 6),
                (11, 7),
                (12, 7),
                (13, 6),
            ],
            fill=SILVER,
        )
        r((10, 6, 10, 6), fill=WHITE)
        line([(11, 8), (8, 12)], fill=WHITE)
        r((9, 13, 9, 13), fill=STEEL)
    elif size == 24:
        p([(5, 3), (13, 3), (17, 7), (17, 19), (15, 21), (4, 21), (3, 20), (3, 5)], fill=INK)
        p([(5, 3), (13, 3), (16, 6), (16, 19), (14, 20), (4, 20), (3, 19), (3, 5)], fill="#747E91")
        p(
            [(13, 3), (16, 6), (16, 19), (14, 20), (4, 20), (3, 19), (14, 19), (15, 18), (15, 6)],
            fill="#202C3D",
        )
        p([(5, 4), (13, 4), (15, 6), (15, 18), (14, 19), (4, 19), (4, 5)], fill="#46546A")
        line([(5, 3), (12, 3), (14, 5)], fill="#B3B6C1")
        line([(3, 5), (3, 18)], fill="#9EA7B7")
        line([(4, 5), (4, 17)], fill="#617087")

        r((5, 6, 14, 17), fill=INK)
        r((6, 6, 13, 7), fill="#FFBB00")
        r((6, 8, 12, 8), fill="#FF8C00")
        r((6, 10, 6, 11), fill="#FF4510")

        p([(7, 9), (9, 9), (10, 10), (10, 12), (9, 13), (7, 13), (6, 12), (6, 10)], fill=INK)
        r((7, 10, 9, 12), fill="#A9000C")
        p([(7, 10), (8, 10), (9, 11), (8, 12), (7, 11)], fill="#FF2018")
        r((7, 10, 7, 10), fill="#FFF4ED")
        r((8, 13, 8, 14), fill=WHITE)

        # Horizontal top rim and broad side face remove the foreshortening.
        r((5, 15, 12, 17), fill=INK)
        r((6, 15, 11, 15), fill=SILVER)
        r((6, 16, 11, 17), fill="#344055")
        r((6, 17, 11, 17), fill="#202C3D")
        r((10, 14, 10, 14), fill="#FF2018")
        r((5, 18, 13, 20), fill=INK)

        for x in (6, 9, 12):
            r((x, 19, x + 1, 20), fill="#FFD031")

        # Open jaw remains distinct from the cartridge label and joystick.
        p(
            [
                (18, 6),
                (19, 6),
                (22, 9),
                (22, 11),
                (20, 13),
                (16, 20),
                (14, 22),
                (12, 21),
                (11, 19),
                (15, 12),
                (14, 11),
                (14, 9),
                (15, 8),
                (17, 10),
                (18, 10),
                (19, 9),
                (18, 8),
            ],
            fill=INK,
        )
        p(
            [
                (18, 7),
                (19, 7),
                (21, 9),
                (21, 11),
                (19, 13),
                (15, 20),
                (14, 21),
                (12, 20),
                (12, 19),
                (16, 12),
                (15, 11),
                (15, 9),
                (17, 11),
                (19, 10),
                (20, 9),
            ],
            fill=STEEL,
        )
        p(
            [
                (18, 7),
                (19, 7),
                (20, 8),
                (21, 10),
                (20, 11),
                (18, 13),
                (14, 20),
                (13, 20),
                (12, 19),
                (16, 12),
                (15, 11),
                (15, 9),
                (17, 11),
                (19, 11),
                (20, 9),
            ],
            fill=SILVER,
        )
        line([(15, 9), (15, 11), (16, 12), (12, 19), (13, 20)], fill=WHITE)
        line([(18, 13), (14, 19)], fill=STEEL)
        r((14, 20, 14, 20), fill=INK)
    else:
        p([(6, 4), (18, 4), (23, 9), (23, 26), (20, 29), (6, 29), (4, 27), (4, 6)], fill=INK)
        p([(6, 4), (18, 4), (22, 8), (22, 26), (20, 28), (6, 28), (4, 26), (4, 6)], fill="#747E91")
        p(
            [(18, 4), (22, 8), (22, 26), (20, 28), (6, 28), (4, 26), (19, 27), (21, 25), (21, 9)],
            fill="#202C3D",
        )
        p([(6, 5), (17, 5), (21, 9), (21, 25), (19, 27), (6, 27), (5, 26), (5, 6)], fill="#46546A")
        p([(6, 5), (17, 5), (18, 6), (7, 6), (6, 7), (6, 25), (5, 25), (5, 6)], fill="#617087")
        line([(6, 4), (17, 4), (19, 6)], fill="#B3B6C1")
        line([(4, 7), (4, 24)], fill="#9EA7B7")
        line([(6, 28), (19, 28), (21, 26)], fill="#18222F")
        r((7, 7, 19, 23), fill=INK)
        r((8, 8, 18, 9), fill="#FFBB00")
        r((8, 10, 17, 11), fill="#FF8C00")
        r((8, 12, 15, 13), fill="#FF4510")
        p([(10, 12), (11, 12), (13, 14), (13, 15), (11, 17), (10, 17), (8, 15), (8, 14)], fill=INK)
        p(
            [(10, 13), (11, 13), (12, 14), (12, 15), (11, 16), (10, 16), (9, 15), (9, 14)],
            fill="#A9000C",
        )
        p([(10, 13), (11, 13), (11, 14), (10, 15), (9, 14)], fill="#FF2018")
        r((10, 13, 10, 13), fill="#FFF4ED")
        r((11, 17, 11, 19), fill=WHITE)

        # A level deck reads as a side view without changing the upright handle.
        r((7, 20, 17, 23), fill=INK)
        r((8, 20, 16, 20), fill=SILVER)
        r((8, 21, 16, 22), fill="#344055")
        r((8, 23, 16, 23), fill="#202C3D")
        r((14, 19, 15, 20), fill="#A9000C")
        r((14, 19, 15, 19), fill="#FF2018")
        r((14, 19, 14, 19), fill="#FF5140")
        r((7, 24, 17, 27), fill=INK)
        line([(7, 24), (17, 24)], fill=STEEL)

        for x in (8, 10, 12, 14, 16):
            r((x, 25, x, 26), fill="#FFD031")

        p(
            [
                (24, 8),
                (26, 9),
                (29, 12),
                (29, 15),
                (26, 18),
                (21, 27),
                (19, 29),
                (17, 28),
                (16, 26),
                (17, 24),
                (22, 16),
                (20, 14),
                (20, 12),
                (21, 10),
                (23, 12),
                (24, 12),
                (25, 11),
                (24, 10),
            ],
            fill=INK,
        )
        p(
            [
                (24, 9),
                (26, 10),
                (28, 12),
                (28, 15),
                (25, 18),
                (20, 27),
                (19, 28),
                (17, 27),
                (17, 25),
                (23, 16),
                (21, 14),
                (21, 12),
                (23, 13),
                (24, 13),
                (26, 11),
            ],
            fill=STEEL,
        )
        p(
            [
                (24, 9),
                (26, 10),
                (27, 12),
                (27, 14),
                (25, 16),
                (24, 17),
                (20, 25),
                (19, 27),
                (18, 27),
                (17, 26),
                (23, 16),
                (22, 15),
                (21, 13),
                (21, 12),
                (23, 14),
                (25, 13),
                (26, 11),
            ],
            fill=SILVER,
        )
        line([(24, 9), (26, 10), (27, 12)], fill=WHITE)
        line([(21, 12), (21, 14), (23, 16), (17, 26), (18, 27)], fill=WHITE)
        line([(24, 17), (20, 24)], fill=STEEL)
        r((18, 26, 19, 26), fill=INK)

    return im


def build_frames():
    master = draw_master()
    frames = {size: master.resize((size, size), Image.Resampling.NEAREST) for size in (48, 64, 128)}
    frames[256] = master
    frames.update({size: draw_small(size) for size in (16, 24, 32)})

    return frames


def main():
    frames = build_frames()
    RESOURCES.mkdir(parents=True, exist_ok=True)
    frames[256].save(RESOURCES / "havGamePatcher.png")
    frames[256].save(
        RESOURCES / "havGamePatcher.ico",
        format="ICO",
        sizes=[(size, size) for size in SIZES],
        append_images=[frames[size] for size in SIZES if size != 256],
    )

    # Verify the actual packaged entries retain every custom pixel and alpha.
    with Image.open(RESOURCES / "havGamePatcher.ico") as ico:
        assert ico.ico.sizes() == {(size, size) for size in SIZES}

        for size, frame in frames.items():
            decoded = ico.ico.getimage((size, size)).convert("RGBA")
            assert decoded.tobytes() == frame.tobytes(), f"ICO changed {size}px frame"
            assert set(decoded.getchannel("A").tobytes()) == {0, 255}
            assert decoded.getpixel((0, 0))[3] == 0

    print("Created havGamePatcher.png (256px RGBA) and havGamePatcher.ico")
    print("Verified ICO frames: " + ", ".join(f"{s}x{s}" for s in SIZES))


if __name__ == "__main__":
    main()
