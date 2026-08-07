#!/usr/bin/env python3
"""Check the ROIFT_GUI numpy importer against numpy itself.

Builds fixtures covering the container variants the GUI is expected to open
(compressed/uncompressed, float16/float64/big-endian, Fortran order, 3D/4D) and
the geometry sources it recovers spacing from, then compares every voxel the
probe reports against the array numpy holds.

    cmake -S . -B build -DBUILD_ROIFT_TESTS=ON && cmake --build build
    python scripts/npz_import_selftest.py build/npz_import_probe

Registered as the `npz_import` CTest test when BUILD_ROIFT_TESTS=ON.
Exits 77 (the CTest skip code) when numpy or SimpleITK is unavailable.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP_EXIT_CODE = 77

try:
    import numpy as np
    import SimpleITK as sitk
except ImportError as exc:  # pragma: no cover - environment dependent
    print(f"SKIP: numpy/SimpleITK unavailable ({exc})")
    sys.exit(SKIP_EXIT_CODE)

SIZE_X, SIZE_Y, SIZE_Z = 16, 20, 12
SPACING = (0.7, 0.65, 1.25)
ORIGIN = (-11.5, 3.25, 42.0)


def coordinate_grid() -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    return np.meshgrid(
        np.arange(SIZE_X), np.arange(SIZE_Y), np.arange(SIZE_Z), indexing="ij"
    )


def reference_volume() -> np.ndarray:
    """(X, Y, Z) volume whose value encodes its own coordinates."""
    x, y, z = coordinate_grid()
    return (x * 10000 + y * 100 + z).astype(np.float32)


def float16_safe_volume() -> np.ndarray:
    """Position-dependent (X, Y, Z) volume that float16 stores exactly.

    The coordinate encoding above overflows float16, which would make the
    comparison inf == inf and test nothing. Integers below 2048 are exact, and
    weighting the axes differently still detects a transposed read.
    """
    x, y, z = coordinate_grid()
    return ((x % 8) + 8 * (y % 8) + 64 * (z % 8)).astype(np.float32)


def write_reference_nifti(path: Path, volume_xyz: np.ndarray) -> None:
    image = sitk.GetImageFromArray(np.transpose(volume_xyz, (2, 1, 0)))
    image.SetSpacing(SPACING)
    image.SetOrigin(ORIGIN)
    sitk.WriteImage(image, str(path))


def build_fixtures(root: Path) -> list[dict]:
    """Return the cases to run: fixture files plus what the importer must report."""
    xyz = reference_volume()
    zyx = np.transpose(xyz, (2, 1, 0))
    cases: list[dict] = []

    # nnUNet shape: (C, Z, Y, X) float16, compressed, with the volume beside it.
    half_xyz = float16_safe_volume()
    half_zyx = np.transpose(half_xyz, (2, 1, 0))
    softmax = np.stack([np.zeros_like(half_zyx), half_zyx], axis=0).astype(np.float16)
    np.savez_compressed(root / "nnunet.npz", probabilities=softmax)
    write_reference_nifti(root / "nnunet.nii.gz", xyz)
    cases.append(
        dict(
            name="nnunet-softmax-float16-sibling",
            path=root / "nnunet.npz",
            args=["--channel", "1"],
            expected=half_xyz,
            spacing=SPACING,
            axis_order="zyx",
            geometry_resolved=True,
            array="probabilities",
            channel_count=2,
        )
    )

    # nibabel shape: (X, Y, Z), sibling volume must flip the detected order.
    np.savez(root / "nibabel.npz", data=xyz)
    write_reference_nifti(root / "nibabel.nii.gz", xyz)
    cases.append(
        dict(
            name="nibabel-xyz-sibling",
            path=root / "nibabel.npz",
            args=[],
            expected=xyz,
            spacing=SPACING,
            axis_order="xyz",
            geometry_resolved=True,
            array="data",
            channel_count=1,
        )
    )

    # Sidecar JSON is the only geometry available, and pins the axis order.
    np.savez(root / "sidecar.npz", data=xyz)
    (root / "sidecar.json").write_text(
        json.dumps({"spacing": list(SPACING), "axis_order": "xyz"})
    )
    cases.append(
        dict(
            name="sidecar-json",
            path=root / "sidecar.npz",
            args=[],
            expected=xyz,
            spacing=SPACING,
            axis_order="xyz",
            geometry_resolved=True,
            array="data",
            channel_count=1,
        )
    )

    # Nothing to recover geometry from: must fall back and say so.
    np.savez(root / "bare.npz", data=zyx)
    cases.append(
        dict(
            name="bare-no-geometry",
            path=root / "bare.npz",
            args=[],
            expected=xyz,
            spacing=(1.0, 1.0, 1.0),
            axis_order="zyx",
            geometry_resolved=False,
            array="data",
            channel_count=1,
        )
    )

    # Fortran order, float64, uncompressed.
    np.savez(root / "fortran.npz", data=np.asfortranarray(zyx.astype(np.float64)))
    cases.append(
        dict(
            name="fortran-float64",
            path=root / "fortran.npz",
            args=["--spacing", "0.5", "0.5", "2.0"],
            expected=xyz,
            spacing=(0.5, 0.5, 2.0),
            axis_order="zyx",
            geometry_resolved=True,
            array="data",
            channel_count=1,
        )
    )

    # Big-endian, channel-last 4D, read under an explicit XYZ order.
    channel_last = np.stack([xyz, xyz * 0], axis=3).astype(">f4")
    np.savez_compressed(root / "bigendian.npz", volume=channel_last)
    cases.append(
        dict(
            name="bigendian-channel-last",
            path=root / "bigendian.npz",
            args=["--order", "xyz", "--channel", "0", "--spacing", "1", "1", "1"],
            expected=xyz,
            spacing=(1.0, 1.0, 1.0),
            axis_order="xyz",
            geometry_resolved=True,
            array="volume",
            channel_count=2,
        )
    )

    # Bare .npy holding a uint8 mask: must be classified as a mask.
    mask = (zyx % 2 == 0).astype(np.uint8)
    np.save(root / "mask.npy", mask)
    cases.append(
        dict(
            name="bare-npy-uint8-mask",
            path=root / "mask.npy",
            args=[],
            expected=np.transpose(mask, (2, 1, 0)).astype(np.float32),
            spacing=(1.0, 1.0, 1.0),
            axis_order="zyx",
            geometry_resolved=False,
            array="",
            channel_count=1,
            is_mask=True,
        )
    )

    # Stacked-DICOM layout (rows, columns, slices). Neither ZYX nor XYZ can
    # express it, which is exactly the case that shipped broken.
    np.savez(root / "rowscols.npz", data=np.transpose(xyz, (1, 0, 2)))
    cases.append(
        dict(
            name="yxz-rows-columns-slices",
            path=root / "rowscols.npz",
            args=["--order", "yxz", "--spacing", "1", "1", "1"],
            expected=xyz,
            spacing=(1.0, 1.0, 1.0),
            axis_order="yxz",
            geometry_resolved=True,
            array="data",
            channel_count=1,
        )
    )

    # Mirroring: every image axis reversed must undo a reversed source array.
    np.savez(root / "mirrored.npz", data=xyz[::-1, ::-1, ::-1].copy())
    cases.append(
        dict(
            name="flip-all-three-axes",
            path=root / "mirrored.npz",
            args=["--order", "xyz", "--flip", "xyz", "--spacing", "1", "1", "1"],
            expected=xyz,
            spacing=(1.0, 1.0, 1.0),
            axis_order="xyz",
            geometry_resolved=True,
            array="data",
            channel_count=1,
            flip="XYZ",
        )
    )

    # Several importable arrays: the named one must win over the auto-pick.
    np.savez(root / "multi.npz", seg=mask, probabilities=softmax, extra=zyx)
    cases.append(
        dict(
            name="explicit-array-selection",
            path=root / "multi.npz",
            args=["--array", "extra", "--spacing", "1", "1", "1"],
            expected=xyz,
            spacing=(1.0, 1.0, 1.0),
            axis_order="zyx",
            geometry_resolved=True,
            array="extra",
            channel_count=1,
        )
    )
    return cases


def run_case(probe: Path, case: dict, dump: Path) -> list[str]:
    """Run one case and return the list of failures it produced."""
    command = [str(probe), str(case["path"]), str(dump), *case["args"]]
    finished = subprocess.run(command, capture_output=True, text=True)
    if finished.returncode != 0:
        return [f"probe failed ({finished.returncode}): {finished.stdout}{finished.stderr}"]

    reported = dict(
        line.split("=", 1)
        for line in finished.stdout.splitlines()
        if "=" in line
    )
    failures: list[str] = []

    expected = case["expected"]
    got = np.fromfile(dump, dtype=np.float32)
    if got.size != expected.size:
        failures.append(f"voxel count {got.size} != {expected.size}")
    else:
        # The probe walks x fastest, matching a Fortran-ordered flatten of (X, Y, Z).
        if not np.array_equal(got, expected.ravel(order="F")):
            bad = int(np.count_nonzero(got != expected.ravel(order="F")))
            failures.append(f"{bad} voxel(s) differ from numpy")

    size = tuple(int(v) for v in reported.get("size", "").split(","))
    if size != (SIZE_X, SIZE_Y, SIZE_Z):
        failures.append(f"size {size} != {(SIZE_X, SIZE_Y, SIZE_Z)}")

    spacing = tuple(float(v) for v in reported.get("spacing", "").split(","))
    if not np.allclose(spacing, case["spacing"], rtol=1e-4):
        failures.append(f"spacing {spacing} != {case['spacing']}")

    if reported.get("axis_order", "").lower() != case["axis_order"].lower():
        failures.append(f"axis_order {reported.get('axis_order')!r} != {case['axis_order']!r}")
    if reported.get("array", "") != case["array"]:
        failures.append(f"array {reported.get('array')!r} != {case['array']!r}")

    if int(reported.get("geometry_resolved", "-1")) != int(case["geometry_resolved"]):
        failures.append(
            f"geometry_resolved {reported.get('geometry_resolved')} != {int(case['geometry_resolved'])}"
        )
    if int(reported.get("channel_count", "-1")) != case["channel_count"]:
        failures.append(
            f"channel_count {reported.get('channel_count')} != {case['channel_count']}"
        )
    if reported.get("flip", "---") != case.get("flip", "---"):
        failures.append(f"flip {reported.get('flip')!r} != {case.get('flip', '---')!r}")
    if "is_mask" in case and int(reported.get("is_mask", "-1")) != int(case["is_mask"]):
        failures.append(f"is_mask {reported.get('is_mask')} != {int(case['is_mask'])}")

    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe", type=Path, help="path to the npz_import_probe binary")
    args = parser.parse_args()

    if not args.probe.is_file():
        print(f"SKIP: probe binary not found at {args.probe}")
        return SKIP_EXIT_CODE

    failed = 0
    with tempfile.TemporaryDirectory(prefix="npz_selftest_") as tmp:
        root = Path(tmp)
        cases = build_fixtures(root)
        for case in cases:
            failures = run_case(args.probe, case, root / "dump.raw")
            status = "PASS" if not failures else "FAIL"
            print(f"[{status}] {case['name']}")
            for failure in failures:
                print(f"        {failure}")
            failed += bool(failures)

    print(f"\n{len(cases) - failed}/{len(cases)} cases passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
