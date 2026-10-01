"""Create distinct icons for full-index throughput tests (developer-only Pillow)."""
from pathlib import Path
from PIL import Image, ImageDraw
import argparse

parser = argparse.ArgumentParser()
parser.add_argument('source', type=Path, help='Output of generate_asset_fixtures.py')
parser.add_argument('destination', type=Path, help='A new benchmark image directory')
args = parser.parse_args()
sources = sorted(args.source.glob('*.png'))
if len(sources) != 96:
    parser.error('Expected exactly 96 procedural asset fixtures')
if args.destination.exists():
    parser.error('Use a new destination directory')
args.destination.mkdir(parents=True)
for i in range(1024):
    with Image.open(sources[i % len(sources)]) as source:
        image = source.convert('RGBA')
    # Distinct visible pixels prevent exact-input reuse from inflating throughput.
    ImageDraw.Draw(image).rectangle((58, 54, 68, 62),
                                   fill=(i % 256, (i // 256) * 55 + 30, (i * 31) % 256, 255))
    image.save(args.destination / f'{i:05}.png')
print('Generated 1,024 distinct procedural icons')
