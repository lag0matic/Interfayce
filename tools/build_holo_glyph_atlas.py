"""Build the runtime Holo Glass glyph atlas from the approved concept sheets."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter


ROOT = Path(__file__).resolve().parents[1]
CONCEPTS = ROOT / "design" / "icon-concepts"
OUTPUT = ROOT / "assets" / "ui" / "holo-glyph-atlas.png"
CELL = 72
COLUMNS = 8


@dataclass(frozen=True)
class Glyph:
    name: str
    source: str
    center: tuple[int, int]
    crop: tuple[int, int] = (106, 96)


def row(names: tuple[str, ...], xs: tuple[int, ...], y: int,
        crop: tuple[int, int]) -> list[Glyph]:
    return [Glyph(name, "holo-glass.png", (x, y), crop)
            for name, x in zip(names, xs, strict=True)]


GLYPHS = [
    *row(("music", "comms", "desktop", "playspace", "rig", "settings"),
         (100, 230, 361, 492, 631, 762), 204, (106, 100)),
    *row(("previous", "play", "pause", "next", "broadcast", "voice_mic"),
         (100, 231, 362, 493, 625, 756), 358, (106, 96)),
    *row(("new_surface", "keyboard", "surface_stack", "back", "bring_all",
          "return_picker", "lock", "unlock", "bring_view", "close", "favorite"),
         (100, 231, 362, 493, 625, 756, 887, 1018, 1149, 1280, 1411), 510,
         (106, 92)),
    *row(("comms_mic", "clear_chat", "playspace_restore", "rig_reset", "rig_mount",
          "desktop_settings", "volume_down", "volume_up", "speaker", "mute"),
         (98, 216, 335, 449, 552, 659, 765, 875, 985, 1095), 663, (106, 88)),
    Glyph("broadcast_gain_down", "holo-glass-distinct-pairs.png", (239, 282), (330, 310)),
    Glyph("broadcast_gain_up", "holo-glass-distinct-pairs.png", (638, 282), (330, 310)),
    Glyph("shutdown", "holo-glass.png", (1435, 663), (106, 88)),
    Glyph("copy", "holo-glass-distinct-pairs.png", (1040, 282), (330, 310)),
    Glyph("paste", "holo-glass-distinct-pairs.png", (1437, 282), (330, 310)),
    Glyph("assistant", "", (0, 0), (72, 72)),
]


def assistant_glyph_image() -> Image.Image:
    """Approved Dialogue Core: clipped glass, speech cell, orb, and waveform."""
    scale = 4
    size = CELL * scale
    tile = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    glow = Image.new("RGBA", tile.size, (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    frame = [(34, 19), (254, 19), (273, 38), (273, 224),
             (254, 243), (34, 243), (15, 224), (15, 38)]
    gd.line(frame + [frame[0]], fill=(115, 67, 255, 220), width=9, joint="curve")
    gd.line([(40, 238), (78, 238)], fill=(0, 226, 255, 240), width=7)
    gd.line([(210, 238), (248, 238)], fill=(0, 226, 255, 240), width=7)
    bubble = [(63, 68), (225, 68), (225, 177), (166, 177),
              (139, 204), (139, 177), (63, 177)]
    gd.line(bubble + [bubble[0]], fill=(155, 93, 255, 255), width=11, joint="curve")
    gd.ellipse((119, 100, 169, 150), fill=(184, 122, 255, 255))
    for offset, height in ((-48, 28), (-31, 44), (31, 44), (48, 28)):
        gd.rounded_rectangle((144 + offset - 5, 125 - height // 2,
                              144 + offset + 5, 125 + height // 2), 5,
                             fill=(174, 105, 255, 245))
    tile.alpha_composite(glow.filter(ImageFilter.GaussianBlur(15)))
    draw = ImageDraw.Draw(tile)
    draw.polygon(frame, fill=(16, 14, 38, 166))
    draw.line(frame + [frame[0]], fill=(151, 112, 255, 235), width=4, joint="curve")
    draw.line([(40, 238), (78, 238)], fill=(21, 215, 255, 255), width=4)
    draw.line([(210, 238), (248, 238)], fill=(21, 215, 255, 255), width=4)
    draw.line(bubble + [bubble[0]], fill=(171, 112, 255, 255), width=7, joint="curve")
    draw.ellipse((121, 102, 167, 148), fill=(207, 174, 255, 255),
                 outline=(255, 255, 255, 245), width=3)
    for offset, height in ((-48, 28), (-31, 44), (31, 44), (48, 28)):
        draw.rounded_rectangle((144 + offset - 4, 125 - height // 2,
                                144 + offset + 4, 125 + height // 2), 4,
                               fill=(184, 123, 255, 255))
    draw.ellipse((136, 232, 152, 248), fill=(198, 251, 255, 255))
    return tile.resize((CELL, CELL), Image.Resampling.LANCZOS)


def glyph_image(source: Image.Image, spec: Glyph) -> Image.Image:
    crop_width, crop_height = spec.crop
    cx, cy = spec.center
    crop = source.crop((cx - crop_width // 2, cy - crop_height // 2,
                          cx + crop_width // 2, cy + crop_height // 2)).convert("RGBA")
    scale = min((CELL - 4) / crop.width, (CELL - 4) / crop.height)
    crop = crop.resize((round(crop.width * scale), round(crop.height * scale)),
                       Image.Resampling.LANCZOS)

    pixels = crop.load()
    for y in range(crop.height):
        for x in range(crop.width):
            red, green, blue, _ = pixels[x, y]
            brightest = max(red, green, blue)
            color_range = brightest - min(red, green, blue)
            signal = max(brightest - 34, color_range - 10)
            alpha = max(0, min(255, signal * 6))
            pixels[x, y] = (red, green, blue, alpha)

    tile = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    tile.alpha_composite(crop, ((CELL - crop.width) // 2, (CELL - crop.height) // 2))
    return tile


def main() -> None:
    sources: dict[str, Image.Image] = {}
    rows = (len(GLYPHS) + COLUMNS - 1) // COLUMNS
    atlas = Image.new("RGBA", (COLUMNS * CELL, rows * CELL), (0, 0, 0, 0))
    for index, spec in enumerate(GLYPHS):
        if spec.name == "assistant":
            glyph = assistant_glyph_image()
        else:
            source = sources.setdefault(
                spec.source, Image.open(CONCEPTS / spec.source).convert("RGBA"))
            glyph = glyph_image(source, spec)
        atlas.alpha_composite(glyph,
                              ((index % COLUMNS) * CELL, (index // COLUMNS) * CELL))
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(OUTPUT, optimize=True)
    print(f"Wrote {len(GLYPHS)} glyphs to {OUTPUT} ({atlas.width}x{atlas.height})")


if __name__ == "__main__":
    main()
