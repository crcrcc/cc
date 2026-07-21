# Unreal RMA Channel Packer

Unreal Engine 5.6 editor plugin that packs three selected `Texture2D` assets
into one mask texture without using external software.

Default channel order:

- **R** = Roughness
- **G** = Metallic
- **B** = Ambient Occlusion

## UE5.6 in-editor workflow

1. Copy or keep `Plugins/RMAChannelPacker` in your Unreal project.
2. Open the project in Unreal Engine 5.6 and enable **RMA Channel Packer** if
   the plugin is not already enabled.
3. In the Content Browser, select exactly three `Texture2D` assets in this
   order, even if they are in different folders: Roughness, Metallic, Ambient
   Occlusion.
4. Right-click the selection and choose **Create RMA Texture**.
5. The plugin creates a new texture next to the Roughness texture. It removes
   common map suffixes such as `_Roughness`, `_R`, `_Metallic`, `_M`, `_AO`,
   and `_Occlusion`, then adds `_RMA`; for example, `T_Wall_Roughness`,
   `T_Wall_Metallic`, and `T_Wall_AO` generate `T_Wall_RMA`.
6. The generated texture uses the first selected Roughness texture resolution.
   If Metallic or AO has a different resolution, the plugin warns you and
   resizes those channels to match Roughness before creating the packed asset.
7. The generated texture uses `TC_Masks`, disables sRGB, and disables mip
   generation.

All source textures must contain CPU-readable source art. Cross-folder Content
Browser selections are supported. Supported source formats are G8, BGRA8,
RGBA16, and RGBA16F.

## Legacy standalone script

`channel_packer.py` remains available for non-Unreal batch use, but the UE5.6
workflow above is the intended no-external-software path.
