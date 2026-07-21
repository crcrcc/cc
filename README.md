# Unreal RMA Channel Packer

A small Python tool for packing three grayscale texture maps into one RGB image
before importing into Unreal Engine:

- **R** = Roughness
- **G** = Metallic
- **B** = Ambient Occlusion

## Install

```bash
python -m pip install Pillow
```

## Artist-friendly picker

Run the script without arguments. It will ask you to choose the three source
images in order, then choose where to save the packed texture.

```bash
python channel_packer.py
```

## Command-line usage

```bash
python channel_packer.py \
  --roughness path/to/roughness.png \
  --metallic path/to/metallic.png \
  --ao path/to/ao.png \
  --output path/to/material_RMA.png
```

By default, Metallic and AO are resized to match the Roughness image. Add
`--no-resize` if you want the tool to fail when the three inputs are not already
the same resolution.
