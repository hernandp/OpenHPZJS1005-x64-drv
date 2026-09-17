"""Verify queue_test captures offline. Requires Pillow and the optional reference build."""
from io import BytesIO
from pathlib import Path
import argparse
import subprocess
from PIL import Image, ImageChops

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--capture-directory", type=Path, default=root / "artifacts")
parser.add_argument("--prefix", default="full-")
parser.add_argument("--reference", type=Path, default=root / "build/Release/reference_foo2zjs.exe")
args = parser.parse_args()
reference = args.reference

def pages(path):
    stream = BytesIO(path.read_bytes())
    result = []
    while stream.tell() < len(stream.getbuffer()):
        assert stream.readline() == b"P4\n", "Unexpected PBM header"
        width, height = map(int, stream.readline().split())
        data = stream.read(((width + 7) // 8) * height)
        assert len(data) == ((width + 7) // 8) * height
        # PBM black bits are one, whereas Pillow mode 1 uses zero for black.
        result.append(ImageChops.invert(Image.frombytes("1", (width, height), data).convert("L")))
    return result

for scenario in ("portrait", "landscape", "blank", "multi", "letter"):
    pbm = args.capture_directory / f"{args.prefix}{scenario}.pbm"
    images = pages(pbm)
    assert len(images) == (3 if scenario == "multi" else 1)
    size = (5100, 6600) if scenario == "letter" else (4960, 7016)
    bounds = (195, 195, size[0] - 195, size[1] - 195)
    for index, image in enumerate(images):
        assert image.size == size
        blank = scenario == "blank" or (scenario == "multi" and index == 1)
        assert ImageChops.invert(image).getbbox() == (None if blank else bounds), scenario
    if scenario == "multi":
        assert ImageChops.difference(images[0], images[2]).getbbox(), "Page-specific markers were lost"
    expected = subprocess.run([str(reference), "-z0", "-P", "-r600x600",
                               "-p1" if scenario == "letter" else "-p9", str(pbm)],
                              capture_output=True, check=True).stdout
    assert expected == pbm.with_suffix(".zjs").read_bytes(), f"Reference differs: {scenario}"
    print(f"{scenario}: geometry, page count, margins and reference bytes passed")
