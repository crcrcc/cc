#!/usr/bin/env python3
"""Pack roughness, metallic, and ambient occlusion maps into one RGB image.

Default channel order:
  R = Roughness
  G = Metallic
  B = Ambient Occlusion

This can be used as a small artist-facing tool before importing textures into
Unreal Engine. Run without arguments to open a file-picker UI, or pass paths on
command line for batch/automation use.
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Iterable

try:
    from PIL import Image
except ImportError as exc:  # pragma: no cover - dependency guidance path
    raise SystemExit(
        "Pillow is required. Install it with: python -m pip install Pillow"
    ) from exc

VALID_EXTENSIONS = {".png", ".jpg", ".jpeg", ".tif", ".tiff", ".bmp", ".tga"}


def _load_grayscale(path: Path, size: tuple[int, int] | None = None) -> Image.Image:
    """Load an image as an 8-bit grayscale channel, optionally resizing it."""
    image = Image.open(path).convert("L")
    if size is not None and image.size != size:
        image = image.resize(size, Image.Resampling.LANCZOS)
    return image


def pack_channels(
    roughness_path: Path,
    metallic_path: Path,
    ao_path: Path,
    output_path: Path,
    *,
    resize_to_roughness: bool = True,
) -> Path:
    """Create an RGB texture with roughness/metallic/AO stored in R/G/B."""
    roughness = _load_grayscale(roughness_path)
    target_size = roughness.size if resize_to_roughness else None
    metallic = _load_grayscale(metallic_path, target_size)
    ao = _load_grayscale(ao_path, target_size)

    if not resize_to_roughness and (roughness.size != metallic.size or roughness.size != ao.size):
        raise ValueError(
            "Input textures must have the same resolution. "
            "Use --resize-to-roughness to resize Metallic and AO to Roughness."
        )

    packed = Image.merge("RGB", (roughness, metallic, ao))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    packed.save(output_path)
    return output_path


def _guess_output_path(roughness_path: Path) -> Path:
    stem = roughness_path.stem
    for suffix in ("_roughness", "_Roughness", "_rough", "_Rough", "_r", "_R"):
        if stem.endswith(suffix):
            stem = stem[: -len(suffix)]
            break
    return roughness_path.with_name(f"{stem}_RMA.png")


def _run_gui() -> int:
    """Open a simple three-step picker for artists."""
    import tkinter as tk
    from tkinter import filedialog, messagebox

    root = tk.Tk()
    root.withdraw()

    def pick(title: str) -> Path | None:
        selected = filedialog.askopenfilename(
            title=title,
            filetypes=[("Image files", "*.png *.jpg *.jpeg *.tif *.tiff *.bmp *.tga"), ("All files", "*.*")],
        )
        return Path(selected) if selected else None

    roughness = pick("1/3 Select Roughness map (goes to Red channel)")
    if roughness is None:
        return 1
    metallic = pick("2/3 Select Metallic map (goes to Green channel)")
    if metallic is None:
        return 1
    ao = pick("3/3 Select Ambient Occlusion map (goes to Blue channel)")
    if ao is None:
        return 1

    output = filedialog.asksaveasfilename(
        title="Save packed RMA texture",
        initialfile=_guess_output_path(roughness).name,
        defaultextension=".png",
        filetypes=[("PNG", "*.png"), ("TGA", "*.tga"), ("TIFF", "*.tif *.tiff"), ("All files", "*.*")],
    )
    if not output:
        return 1

    try:
        result = pack_channels(roughness, metallic, ao, Path(output))
    except Exception as exc:  # pragma: no cover - GUI reporting path
        messagebox.showerror("Packing failed", str(exc))
        return 2

    messagebox.showinfo(
        "Packing complete",
        f"Saved:\n{result}\n\nChannels:\nR = Roughness\nG = Metallic\nB = Ambient Occlusion",
    )
    return 0


def _validate_image_path(path_text: str) -> Path:
    path = Path(path_text).expanduser()
    if not path.is_file():
        raise argparse.ArgumentTypeError(f"File does not exist: {path}")
    if path.suffix.lower() not in VALID_EXTENSIONS:
        raise argparse.ArgumentTypeError(f"Unsupported image extension: {path.suffix}")
    return path


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Pack Roughness, Metallic, and AO maps into RGB channels for Unreal Engine.",
    )
    parser.add_argument("--roughness", type=_validate_image_path, help="R channel input image")
    parser.add_argument("--metallic", type=_validate_image_path, help="G channel input image")
    parser.add_argument("--ao", type=_validate_image_path, help="B channel input image")
    parser.add_argument("--output", type=Path, help="Output image path, e.g. MyMaterial_RMA.png")
    parser.add_argument(
        "--no-resize",
        action="store_true",
        help="Require all inputs to already match instead of resizing Metallic/AO to Roughness size.",
    )
    return parser


def main(argv: Iterable[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    provided = [args.roughness, args.metallic, args.ao, args.output]
    if not any(provided):
        return _run_gui()
    if not all(provided):
        parser.error("Use all of --roughness, --metallic, --ao, and --output, or use no args for GUI mode.")

    result = pack_channels(
        args.roughness,
        args.metallic,
        args.ao,
        args.output,
        resize_to_roughness=not args.no_resize,
    )
    print(f"Saved packed texture: {result}")
    print("Channels: R=Roughness, G=Metallic, B=Ambient Occlusion")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
