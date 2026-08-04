"""Build the runtime Holo Glass glyph atlas from the approved concept sheets."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from PIL import Image


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
]


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
        source = sources.setdefault(spec.source, Image.open(CONCEPTS / spec.source).convert("RGBA"))
        atlas.alpha_composite(glyph_image(source, spec),
                              ((index % COLUMNS) * CELL, (index // COLUMNS) * CELL))
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(OUTPUT, optimize=True)
    print(f"Wrote {len(GLYPHS)} glyphs to {OUTPUT} ({atlas.width}x{atlas.height})")


if __name__ == "__main__":
    main()
