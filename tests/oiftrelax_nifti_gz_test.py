#!/usr/bin/env python3
"""Segment a synthetic .nii.gz sphere with oiftrelax and check the mask.

Guards gzip NIfTI I/O in the shipped binary: a gft built with ZLIB_NOT_FOUND
reads a .nii.gz header as raw bytes and segfaults. Standard library only, so it
runs wherever CTest does.

usage: oiftrelax_nifti_gz_test.py <path-to-oiftrelax>
"""
import gzip
import os
import struct
import subprocess
import sys
import tempfile

N = 40            # volume edge, voxels
RADIUS = 10       # bright sphere radius, voxels
MIN_DICE = 0.9    # the reference build scores 0.992 on this phantom

# NIfTI-1 datatype code -> struct format character
DTYPES = {2: "B", 4: "h", 8: "i", 16: "f", 512: "H", 768: "I"}


def write_nifti_gz(path, values):
    hdr = bytearray(348)
    struct.pack_into("<i", hdr, 0, 348)                             # sizeof_hdr
    struct.pack_into("<8h", hdr, 40, 3, N, N, N, 1, 1, 1, 1)        # dim
    struct.pack_into("<hh", hdr, 70, 4, 16)                         # int16
    struct.pack_into("<8f", hdr, 76, 1, 1, 1, 1, 1, 1, 1, 1)        # pixdim, mm
    struct.pack_into("<f", hdr, 108, 352.0)                         # vox_offset
    struct.pack_into("<f", hdr, 112, 1.0)                           # scl_slope
    hdr[344:348] = b"n+1\0"
    with gzip.open(path, "wb") as f:
        f.write(bytes(hdr) + b"\0" * 4 + struct.pack(f"<{len(values)}h", *values))


def read_nifti_gz(path):
    raw = gzip.open(path, "rb").read()
    dim = struct.unpack_from("<8h", raw, 40)
    dtype = struct.unpack_from("<h", raw, 70)[0]
    offset = int(struct.unpack_from("<f", raw, 108)[0])
    count = dim[1] * dim[2] * dim[3]
    if dtype not in DTYPES:
        sys.exit(f"FAIL: unexpected output datatype {dtype}")
    return dim[1:4], struct.unpack_from(f"<{count}{DTYPES[dtype]}", raw, offset)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    oiftrelax = os.path.abspath(sys.argv[1])

    # NIfTI stores x fastest, so voxel (x, y, z) is index x + N*(y + N*z).
    inside = [(x - N // 2) ** 2 + (y - N // 2) ** 2 + (z - N // 2) ** 2 <= RADIUS ** 2
              for z in range(N) for y in range(N) for x in range(N)]

    with tempfile.TemporaryDirectory() as work:
        volume = os.path.join(work, "sphere.nii.gz")
        seeds = os.path.join(work, "seeds.txt")
        output = os.path.join(work, "mask.nii.gz")
        write_nifti_gz(volume, [1000 if v else 0 for v in inside])
        # x y z marker label: object seeds at the centre, background in two corners
        rows = [(20, 20, 20, 1, 1), (21, 20, 20, 1, 1), (2, 2, 2, 0, 0), (37, 37, 37, 0, 0)]
        with open(seeds, "w") as f:
            f.write(f"{len(rows)}\n" + "".join(" ".join(map(str, r)) + "\n" for r in rows))

        # Positive polarity: the sphere is brighter than its surroundings. A
        # bright target takes percentile 0, and niter 0 skips relaxation.
        proc = subprocess.run([oiftrelax, volume, seeds, "1.0", "0", "0", output],
                              capture_output=True, text=True)
        if proc.returncode != 0:
            tail = "\n".join((proc.stdout + proc.stderr).splitlines()[-5:])
            sys.exit(f"FAIL: oiftrelax exited {proc.returncode}\n{tail}")
        if not os.path.exists(output):
            sys.exit("FAIL: oiftrelax wrote no output")

        shape, labels = read_nifti_gz(output)

    if shape != (N, N, N):
        sys.exit(f"FAIL: output shape {shape}, expected {(N, N, N)}")
    if not set(labels) <= {0, 1}:
        sys.exit(f"FAIL: unexpected labels {sorted(set(labels))}")
    seg = [v > 0 for v in labels]
    both = sum(a and b for a, b in zip(seg, inside))
    dice = 2 * both / (sum(seg) + sum(inside))
    print(f"sphere {sum(inside)} voxels, segmented {sum(seg)}, dice {dice:.3f}")
    if dice < MIN_DICE:
        sys.exit(f"FAIL: dice {dice:.3f} below {MIN_DICE}")


if __name__ == "__main__":
    main()
