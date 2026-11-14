#!/usr/bin/env python3
"""Validate a local NDF DDS pair and optionally stage it as an asset directory."""

import argparse
import hashlib
import shutil
import struct
from pathlib import Path

# DXGI format: channels, block bytes (0 for uncompressed), bits per pixel.
FORMATS = {
    2: (4, 0, 128), 6: (3, 0, 96), 10: (4, 0, 64), 11: (4, 0, 64),
    16: (2, 0, 64), 28: (4, 0, 32), 34: (2, 0, 32), 35: (2, 0, 32),
    41: (1, 0, 32), 49: (2, 0, 16), 54: (1, 0, 16), 56: (1, 0, 16),
    61: (1, 0, 8), 71: (4, 8, 0), 74: (4, 16, 0), 77: (4, 16, 0),
    80: (1, 8, 0), 83: (2, 16, 0), 87: (4, 0, 32), 88: (3, 0, 32), 98: (4, 16, 0),
}


def inspect(path, channels):
    data = path.read_bytes()
    if len(data) < 128 or data[:4] != b"DDS " or struct.unpack_from("<I", data, 4)[0] != 124:
        raise ValueError("Invalid DDS header")
    height, width = struct.unpack_from("<II", data, 12)
    mips = max(struct.unpack_from("<I", data, 28)[0], 1)
    fourcc = data[84:88]
    offset = 128
    if fourcc == b"DX10":
        if len(data) < 148:
            raise ValueError("Truncated DX10 header")
        fmt, dimension, flags, count, _ = struct.unpack_from("<5I", data, 128)
        if dimension != 3 or count != 1 or flags & 4:
            raise ValueError("Expected one 2D texture, not a volume, array or cubemap")
        offset = 148
    else:
        if struct.unpack_from("<I", data, 112)[0] & (0x200000 | 0xFE00):
            raise ValueError("Volume or cubemap DDS is not an NDF input")
        legacy = {b"DXT1": 71, b"DXT3": 74, b"DXT5": 77, b"ATI1": 80, b"BC4U": 80, b"ATI2": 83, b"BC5U": 83}
        if fourcc not in legacy:
            raise ValueError("This inspection tool requires DX10 or a recognized legacy BC header; runtime also accepts linear legacy uncompressed DDS")
        fmt = legacy[fourcc]
    if fmt not in FORMATS or FORMATS[fmt][0] < channels:
        raise ValueError(f"DXGI {fmt}: unsupported linear format or insufficient channels")
    if min(width, height) < 1 or max(width, height) > 16384 or mips > max(width, height).bit_length():
        raise ValueError("Invalid dimensions or mip count")
    _, block, bits = FORMATS[fmt]
    payload = 0
    for mip in range(mips):
        w, h = max(width >> mip, 1), max(height >> mip, 1)
        payload += ((w + 3) // 4) * ((h + 3) // 4) * block if block else w * h * bits // 8
    if len(data) < offset + payload:
        raise ValueError("Truncated mip payload")
    return {"width": width, "height": height, "mips": mips, "format": fmt, "sha256": hashlib.sha256(data).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("height", type=Path)
    parser.add_argument("modeling", type=Path)
    parser.add_argument("--mask", type=Path)
    parser.add_argument("--output", type=Path, help="Write channel previews and a text report (requires Pillow)")
    parser.add_argument("--stage-root", type=Path, help="Asset directory to receive Height.dds, Modeling.dds and optional Mask.dds")
    args = parser.parse_args()
    inputs = {"Height": (args.height, 2), "Modeling": (args.modeling, 3)}
    if args.mask:
        inputs["Mask"] = (args.mask, 1)
    metadata = {name: inspect(path, channels) for name, (path, channels) in inputs.items()}
    if (metadata["Height"]["width"], metadata["Height"]["height"]) != (metadata["Modeling"]["width"], metadata["Modeling"]["height"]):
        raise ValueError("Height and Modeling dimensions must match")
    report = [f"{name}: {value}" for name, value in metadata.items()]
    if args.output:
        from PIL import Image, ImageDraw
        import numpy as np
        args.output.mkdir(parents=True, exist_ok=True)
        height = Image.open(args.height).convert("RGB")
        model = Image.open(args.modeling).convert("RGBA")
        h = np.asarray(height, dtype=np.float32) / 255.0
        m = np.asarray(model, dtype=np.float32) / 255.0
        mask = m[..., 3]
        if args.mask:
            mask *= np.asarray(Image.open(args.mask).convert("L").resize(height.size), dtype=np.float32) / 255.0
        visible = mask > 0.01
        report.append(f"Influence footprint > 0.01: {visible.mean():.6f}")
        report.append(f"Reversed heights in footprint: {int(((h[..., 0] > h[..., 1]) & visible).sum())}")
        report.append(f"Empty height intervals in footprint: {int(((h[..., 0] == h[..., 1]) & visible).sum())}")
        tiles = [("Height R", height.getchannel("R")), ("Height G", height.getchannel("G")),
                 ("Coverage R", model.getchannel("R")), ("Top type G", model.getchannel("G")),
                 ("Bottom type B", model.getchannel("B")), ("Influence A", model.getchannel("A"))]
        sheet = Image.new("RGB", (768, 560), (25, 25, 25))
        draw = ImageDraw.Draw(sheet)
        for i, (label, tile) in enumerate(tiles):
            tile.save(args.output / (label.replace(" ", "-") + ".png"))
            x, y = (i % 3) * 256, (i // 3) * 280
            draw.text((x + 8, y + 6), label, fill="white")
            sheet.paste(tile.resize((256, 256)).convert("RGB"), (x, y + 24))
        sheet.save(args.output / "channels.png")
        report.append("Previews decode to 8-bit for inspection; source DDS bytes and mip chains are unchanged.")
        (args.output / "validation.txt").write_text("\n".join(report) + "\n", encoding="utf-8")
    if args.stage_root:
        args.stage_root.mkdir(parents=True, exist_ok=True)
        for name, (source, _) in inputs.items():
            target = args.stage_root / (name + ".dds")
            if target.exists() and target.read_bytes() != source.read_bytes():
                raise ValueError(f"Refusing to overwrite different asset: {target}")
        for name, (source, _) in inputs.items():
            target = args.stage_root / (name + ".dds")
            if not target.exists():
                shutil.copyfile(source, target)
    print("\n".join(report))
    print("DDS contract validation passed; this does not validate GPU execution or rendered appearance.")


if __name__ == "__main__":
    main()
