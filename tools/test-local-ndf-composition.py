"""CPU checks for local NDF endpoint transitions; optional DDS inspection fixture."""

import argparse
from pathlib import Path

import numpy as np


def compose(base, sources, weights, maximum_modes, blend_weights=None, quantize=False):
    if blend_weights is None:
        blend_weights = np.ones(len(sources)) / max(len(sources), 1)
    result = base.copy()
    for source, influence, maximum, blend in zip(sources, weights, maximum_modes, blend_weights):
        stored = source * influence
        opacity = influence
        if quantize:
            stored, opacity = (x.astype(np.float16).astype(float) for x in (stored, opacity))
        shaped = np.maximum(base, stored) if maximum else base * (1 - opacity) + stored
        result += blend * (shaped - base)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--modeling", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    rng = np.random.default_rng(92471)
    base = rng.random((1024, 3))
    worst_half = 0.0
    for count in (1, 2, 4, 8, 16):
        for _ in range(100):
            sources = rng.random((count, 1024, 3))
            influences = rng.random((count, 1024, 1))
            modes = rng.integers(0, 2, count)
            weights = rng.random(count + 1)
            weights /= weights.sum()
            expected = base * weights[-1]
            for source, influence, maximum, weight in zip(sources, influences, modes, weights):
                endpoint = np.maximum(base, source * influence) if maximum else base * (1 - influence) + source * influence
                expected += weight * endpoint
            actual = compose(base, sources, influences, modes, weights[:-1])
            np.testing.assert_allclose(actual, expected, rtol=0, atol=1e-14)
            np.testing.assert_allclose(compose(base, sources[::-1], influences[::-1], modes[::-1], weights[-2::-1]), expected, rtol=0, atol=1e-14)
            actual_half = compose(base, sources, influences, modes, weights[:-1], quantize=True)
            worst_half = max(worst_half, float(np.max(np.abs(actual_half - expected))))
    assert worst_half < 0.002
    print(f"512000 RGB samples, weighted endpoints and order independence: passed; maximum FP16 absolute error {worst_half:.8f}")

    np.testing.assert_array_equal(compose(base, [], [], []), base)
    full = np.ones_like(base)
    influence = np.ones((1024, 1))
    for t in (0.0, 0.1, 0.5, 0.9, 1.0):
        np.testing.assert_allclose(compose(base, [full], [influence], [False], [t]), base * (1 - t) + full * t)
    maximum_midpoint = compose(np.full((1, 3), 0.6), [np.ones((1, 3)), np.zeros((1, 3))],
                               [np.ones((1, 1))] * 2, [True, True], [0.5, 0.5])
    np.testing.assert_allclose(maximum_midpoint, 0.8)
    assert not np.allclose(maximum_midpoint, np.maximum(0.6, 0.5))
    print("Empty endpoint, fade to global, exact endpoints and Maximum midpoint: passed")

    heights = rng.uniform(-1000, 10000, (1024, 2))
    inputs = [rng.random((1024, 2)), rng.random((1024, 2))]
    ranges = [(-500.0, 1500.0), (2300.0, 7000.0)]
    minimum, span = -500.0, 9800.0
    for t in (0.0, 0.25, 0.5, 1.0):
        stored = np.zeros((1024, 2))
        opacity = np.zeros((1024, 1))
        expected = heights.copy()
        for raw, (offset, scale), blend in zip(inputs, ranges, (1 - t, t)):
            influence = rng.random((1024, 1))
            absolute = offset + raw * scale
            stored += blend * (absolute - minimum) / span * influence
            opacity += blend * influence
            expected += blend * influence * (absolute - heights)
        decoded = heights * (1 - opacity) + stored * span + minimum * opacity
        np.testing.assert_allclose(decoded, expected, atol=1e-10)
    print("Independent height decoding, reversed intervals and endpoint influence: passed")

    if args.modeling:
        from PIL import Image, ImageDraw

        pixels = np.asarray(Image.open(args.modeling).convert("RGBA"), dtype=float) / 255
        local, alpha = pixels[..., :3], pixels[..., 3:]
        zero = np.zeros_like(local)
        maximum = compose(zero, [local], [np.ones_like(alpha)], [True])
        interpolated = compose(zero, [local], [alpha], [False])
        np.testing.assert_array_equal(maximum, local)
        rejected = (alpha[..., 0] == 0) & (local[..., 0] > 0.5)
        np.testing.assert_array_equal(interpolated[rejected], 0)
        print(f"DDS zero-alpha coverage > 0.5 retained by Maximum: {int(rejected.sum())} texels")
        if args.output:
            args.output.mkdir(parents=True, exist_ok=True)
            size = pixels.shape[0]
            sheet = Image.new("RGB", (size * 3, size + 32), (24, 24, 24))
            draw = ImageDraw.Draw(sheet)
            for i, (label, values) in enumerate((
                ("Source coverage R", local[..., 0]),
                ("Interpolate over zero (R * A)", interpolated[..., 0]),
                ("Maximum over zero (R)", maximum[..., 0]),
            )):
                draw.text((i * size + 8, 8), label, fill="white")
                sheet.paste(Image.fromarray(np.uint8(np.clip(values, 0, 1) * 255)).convert("RGB"), (i * size, 32))
            sheet.save(args.output / "local-coverage-comparison.png")
    print("CPU contract checks passed; GPU output and filtering equivalence are not tested.")


if __name__ == "__main__":
    main()
