#!/usr/bin/env python3
"""Segment a disc in a one-slice volume, or in a 2D NIfTI, with oiftrelax.

Guards the two defects that made a single plane unusable: face seeding that tiled the
whole plane with background (case one_slice), and a reader that refused dim[0] = 2
(case header_2d). Only an object seed is placed, so the background must come from the
plane's edges. Standard library only.

usage: oiftrelax_single_slice_test.py <path-to-oiftrelax> one_slice|header_2d
"""
import gzip
import os
import struct
import subprocess
import sys
import tempfile

N = 64            # plane edge, pixels
RADIUS = 16       # bright disc radius, pixels
MIN_DICE = 0.9

DTYPES = {2: "B", 4: "h", 8: "i", 16: "f", 512: "H", 768: "I"}
# case -> dim[0]; a 2D header zeroes dim[3:] and pixdim[3:], which nifti2_io keeps, so the reader must set nz = 1.
CASES = {"one_slice": 3, "header_2d": 2}


def write_nifti_gz(path, values, ndim):
    hdr = bytearray(348)
    struct.pack_into("<i", hdr, 0, 348)
    dims = (ndim, N, N, 1, 1, 1, 1, 1) if ndim == 3 else (ndim, N, N, 0, 0, 0, 0, 0)
    struct.pack_into("<8h", hdr, 40, *dims)
    struct.pack_into("<hh", hdr, 70, 4, 16)                              # int16
    pix = (1, 1, 1, 1, 1, 1, 1, 1) if ndim == 3 else (1, 1, 1, 0, 0, 0, 0, 0)
    struct.pack_into("<8f", hdr, 76, *pix)
    struct.pack_into("<f", hdr, 108, 352.0)
    struct.pack_into("<f", hdr, 112, 1.0)
    hdr[344:348] = b"n+1\0"
    with gzip.open(path, "wb") as f:
        f.write(bytes(hdr) + b"\0" * 4 + struct.pack(f"<{len(values)}h", *values))


def read_nifti_gz(path):
    with gzip.open(path, "rb") as f:
        raw = f.read()
    dim = struct.unpack_from("<8h", raw, 40)
    dtype = struct.unpack_from("<h", raw, 70)[0]
    offset = int(struct.unpack_from("<f", raw, 108)[0])
    shape = (dim[1], dim[2], max(dim[3], 1) if dim[0] >= 3 else 1)
    if dtype not in DTYPES:
        sys.exit(f"FAIL: unexpected output datatype {dtype}")
    count = shape[0] * shape[1] * shape[2]
    return shape, struct.unpack_from(f"<{count}{DTYPES[dtype]}", raw, offset)


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in CASES:
        sys.exit(__doc__)
    oiftrelax, case = os.path.abspath(sys.argv[1]), sys.argv[2]
    ndim = CASES[case]
    # x fastest: pixel (x, y) is index x + N*y.
    inside = [(x - 32) ** 2 + (y - 32) ** 2 <= RADIUS ** 2 for y in range(N) for x in range(N)]
    with tempfile.TemporaryDirectory() as work:
        volume = os.path.join(work, "plane.nii.gz")
        seeds = os.path.join(work, "seeds.txt")
        output = os.path.join(work, "mask.nii.gz")
        write_nifti_gz(volume, [1000 if v else 0 for v in inside], ndim)
        with open(seeds, "w") as f:
            f.write("1\n32 32 0 1 1\n")
        # Default stride (8) and default blur (2): the defaults are what failed.
        proc = subprocess.run([oiftrelax, volume, seeds, "1.0", "0", "0", output],
                              capture_output=True, text=True)
        if proc.returncode != 0:
            tail = "\n".join((proc.stdout + proc.stderr).splitlines()[-3:])
            sys.exit(f"FAIL: {case}: oiftrelax exited {proc.returncode}\n{tail}")
        shape, labels = read_nifti_gz(output)
    if shape != (N, N, 1):
        sys.exit(f"FAIL: {case}: output shape {shape}, expected {(N, N, 1)}")
    seg = [v > 0 for v in labels]
    both = sum(a and b for a, b in zip(seg, inside))
    dice = 2 * both / (sum(seg) + sum(inside))
    print(f"{case}: disc {sum(inside)} px, segmented {sum(seg)}, dice {dice:.3f}")
    if dice < MIN_DICE:
        sys.exit(f"FAIL: {case}: dice {dice:.3f} below {MIN_DICE}")


if __name__ == "__main__":
    main()
