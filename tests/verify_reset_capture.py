"""Check queue_test emf-reset output against foo2zjs for Letter/A4/Letter.

Usage: python tests/verify_reset_capture.py artifacts/venteveos-reset-fixed.zjs
Requires the optional reference encoder build. No printer access.
"""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
capture = Path(sys.argv[1]).resolve()

def chunks(data):
    assert data[:4] == b"JZJZ"
    offset = 4
    result = []
    while offset < len(data):
        size, kind, count, reserved, signature = struct.unpack_from(">IIIHH", data, offset)
        assert size >= 16 and offset + size <= len(data) and signature == 0x5A5A
        result.append((kind, bytearray(data[offset:offset + size])))
        offset += size
    return result

with tempfile.TemporaryDirectory() as directory:
    pbm = Path(directory) / "decoded.pbm"
    subprocess.run([str(root / "build/Release/lj1005.exe"), "decode", str(capture), str(pbm)], check=True)
    actual = chunks(capture.read_bytes())
    assert [kind for kind, _ in actual].count(0) == 1
    assert [kind for kind, _ in actual].count(1) == 1
    assert [kind for kind, _ in actual].count(2) == 3
    expected = [actual[0]]
    with pbm.open("rb") as source:
        for index, (paper, dimensions) in enumerate([(1, (5100, 6600)), (9, (4960, 7016)), (1, (5100, 6600))], 1):
            assert source.readline() == b"P4\n"
            width, height = map(int, source.readline().split())
            assert (width, height) == dimensions
            pixels = source.read(((width + 7) // 8) * height)
            single = Path(directory) / f"{index}.pbm"
            single.write_bytes(f"P4\n{width} {height}\n".encode() + pixels)
            reference = subprocess.run([str(root / "build/Release/reference_foo2zjs.exe"), "-z0", "-P", "-r600x600", f"-p{paper}", str(single)], capture_output=True, check=True).stdout
            page = chunks(reference)[1:-1]
            # A standalone reference page has sequence 1; preserve this job's sequence.
            start = page[0][1]
            count = struct.unpack_from(">I", start, 8)[0]
            pos = 16
            found = False
            for _ in range(count):
                size, tag = struct.unpack_from(">IH", start, pos)
                if tag == 0x8110:
                    struct.pack_into(">I", start, pos + 8, index)
                    found = True
                pos += size
            assert found
            expected.extend(page)
        assert not source.read()
    expected.append(actual[-1])
    assert actual == expected, "Reset capture differs from reference framing or raster"
print("Letter / A4 landscape / Letter: one document, three pages, reference bytes passed")
