#include "RMAChannelPackerEditorModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "ContentBrowserMenuContexts.h"
#include "IContentBrowserSingleton.h"
#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"
#include "Misc/MessageDialog.h"
#include "Math/Float16.h"
#include "Modules/ModuleManager.h"
#include "TextureCompiler.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "ToolMenus.h"
#include "UObject/SoftObjectPath.h"

#define LOCTEXT_NAMESPACE "RMAChannelPackerEditor"

namespace RMAChannelPacker
{
static constexpr int32 ChannelCount = 4;

static void ShowMessage(const FText& Message, const FText& Title)
{
    FMessageDialog::Open(EAppMsgType::Ok, Message, Title);
}

static void AddTextureAsset(const FAssetData& Asset, TArray<UTexture2D*>& Textures, TSet<FSoftObjectPath>& SeenAssets)
{
    const FSoftObjectPath AssetPath = Asset.ToSoftObjectPath();
    if (SeenAssets.Contains(AssetPath))
    {
        return;
    }

    if (UTexture2D* Texture = Cast<UTexture2D>(Asset.GetAsset()))
    {
        Textures.Add(Texture);
        SeenAssets.Add(AssetPath);
    }
}

static TArray<UTexture2D*> GetSelectedTextures(const FToolMenuContext& MenuContext)
{
    TArray<UTexture2D*> Textures;
    TSet<FSoftObjectPath> SeenAssets;

    FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
    TArray<FAssetData> BrowserSelectedAssets;
    ContentBrowserModule.Get().GetSelectedAssets(BrowserSelectedAssets);
    for (const FAssetData& Asset : BrowserSelectedAssets)
    {
        AddTextureAsset(Asset, Textures, SeenAssets);
    }

    if (Textures.Num() == 0)
    {
        if (const UContentBrowserAssetContextMenuContext* Context = MenuContext.FindContext<UContentBrowserAssetContextMenuContext>())
        {
            for (const FAssetData& Asset : Context->SelectedAssets)
            {
                AddTextureAsset(Asset, Textures, SeenAssets);
            }
        }
    }

    return Textures;
}

static uint8 SampleRedChannelNearest(const TArray64<uint8>& Pixels, int32 SourceWidth, int32 SourceHeight, int32 TargetX, int32 TargetY, int32 TargetWidth, int32 TargetHeight)
{
    const int32 SourceX = FMath::Clamp(FMath::RoundToInt((static_cast<float>(TargetX) + 0.5f) * SourceWidth / TargetWidth - 0.5f), 0, SourceWidth - 1);
    const int32 SourceY = FMath::Clamp(FMath::RoundToInt((static_cast<float>(TargetY) + 0.5f) * SourceHeight / TargetHeight - 0.5f), 0, SourceHeight - 1);
    return Pixels[(static_cast<int64>(SourceY) * SourceWidth + SourceX) * ChannelCount + 0];
}

static bool ReadTexturePixels(UTexture2D* Texture, TArray64<uint8>& OutPixels, int32& OutWidth, int32& OutHeight, FString& OutError)
{
    if (!Texture)
    {
        OutError = TEXT("Texture is invalid.");
        return false;
    }

    FTextureCompilingManager::Get().FinishCompilation({Texture});
    Texture->SetForceMipLevelsToBeResident(30.0f);
    Texture->WaitForStreaming();

    FTextureSource& Source = Texture->Source;
    if (!Source.IsValid())
    {
        OutError = FString::Printf(TEXT("%s has no CPU-readable source art. Reimport the texture or enable source art."), *Texture->GetName());
        return false;
    }

    OutWidth = Source.GetSizeX();
    OutHeight = Source.GetSizeY();
    if (OutWidth <= 0 || OutHeight <= 0)
    {
        OutError = FString::Printf(TEXT("%s has an invalid resolution."), *Texture->GetName());
        return false;
    }

    TArray64<uint8> MipData;
    if (!Source.GetMipData(MipData, 0))
    {
        OutError = FString::Printf(TEXT("Could not read source pixels from %s."), *Texture->GetName());
        return false;
    }

    const ETextureSourceFormat Format = Source.GetFormat();
    OutPixels.SetNumUninitialized(static_cast<int64>(OutWidth) * OutHeight * ChannelCount);

    for (int64 PixelIndex = 0; PixelIndex < static_cast<int64>(OutWidth) * OutHeight; ++PixelIndex)
    {
        uint8 R = 0;
        uint8 G = 0;
        uint8 B = 0;
        uint8 A = 255;

        switch (Format)
        {
        case TSF_G8:
            R = G = B = MipData[PixelIndex];
            break;
        case TSF_BGRA8:
            B = MipData[PixelIndex * 4 + 0];
            G = MipData[PixelIndex * 4 + 1];
            R = MipData[PixelIndex * 4 + 2];
            A = MipData[PixelIndex * 4 + 3];
            break;
        case TSF_RGBA16:
            R = static_cast<uint8>((reinterpret_cast<const uint16*>(MipData.GetData()))[PixelIndex * 4 + 0] >> 8);
            G = static_cast<uint8>((reinterpret_cast<const uint16*>(MipData.GetData()))[PixelIndex * 4 + 1] >> 8);
            B = static_cast<uint8>((reinterpret_cast<const uint16*>(MipData.GetData()))[PixelIndex * 4 + 2] >> 8);
            A = static_cast<uint8>((reinterpret_cast<const uint16*>(MipData.GetData()))[PixelIndex * 4 + 3] >> 8);
            break;
        case TSF_RGBA16F:
        {
            const FFloat16* HalfData = reinterpret_cast<const FFloat16*>(MipData.GetData());
            R = FMath::Clamp(FMath::RoundToInt(HalfData[PixelIndex * 4 + 0].GetFloat() * 255.0f), 0, 255);
            G = FMath::Clamp(FMath::RoundToInt(HalfData[PixelIndex * 4 + 1].GetFloat() * 255.0f), 0, 255);
            B = FMath::Clamp(FMath::RoundToInt(HalfData[PixelIndex * 4 + 2].GetFloat() * 255.0f), 0, 255);
            A = FMath::Clamp(FMath::RoundToInt(HalfData[PixelIndex * 4 + 3].GetFloat() * 255.0f), 0, 255);
            break;
        }
        default:
            OutError = FString::Printf(TEXT("%s uses unsupported source format %d. Use G8, BGRA8, RGBA16, or RGBA16F textures."), *Texture->GetName(), static_cast<int32>(Format));
            return false;
        }

        OutPixels[PixelIndex * 4 + 0] = R;
        OutPixels[PixelIndex * 4 + 1] = G;
        OutPixels[PixelIndex * 4 + 2] = B;
        OutPixels[PixelIndex * 4 + 3] = A;
    }

    return true;
}

static FString BuildSiblingPackagePath(UTexture2D* SourceTexture, const FString& AssetSuffix)
{
    FString PackagePath = SourceTexture->GetOutermost()->GetName();
    FString Path;
    FString Name;
    PackagePath.Split(TEXT("/"), &Path, &Name, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
    return Path / (Name + AssetSuffix);
}

static FString BuildOutputPackagePath(UTexture2D* RoughnessTexture)
{
    FString PackagePath = RoughnessTexture->GetOutermost()->GetName();
    FString Path;
    FString Name;
    PackagePath.Split(TEXT("/"), &Path, &Name, ESearchCase::IgnoreCase, ESearchDir::FromEnd);

    const FString Suffixes[] = {
        TEXT("_Roughness"),
        TEXT("_roughness"),
        TEXT("_Metallic"),
        TEXT("_metallic"),
        TEXT("_Occlusion"),
        TEXT("_occlusion"),
        TEXT("_Rough"),
        TEXT("_rough"),
        TEXT("_AO"),
        TEXT("_ao"),
        TEXT("_M"),
        TEXT("_m"),
        TEXT("_R"),
        TEXT("_r")};
    for (const FString& Suffix : Suffixes)
    {
        if (Name.EndsWith(Suffix))
        {
            Name.LeftChopInline(Suffix.Len());
            break;
        }
    }

    return Path / (Name + TEXT("_RMA"));
}

static FString BuildAlphaOutputPackagePath(UTexture2D* BaseTexture)
{
    return BuildSiblingPackagePath(BaseTexture, TEXT("_Alpha"));
}

static void CopyTextureSettings(UTexture2D* SourceTexture, UTexture2D* OutputTexture)
{
    OutputTexture->CompressionSettings = SourceTexture->CompressionSettings;
    OutputTexture->SRGB = SourceTexture->SRGB;
    OutputTexture->MipGenSettings = SourceTexture->MipGenSettings;
    OutputTexture->LODGroup = SourceTexture->LODGroup;
    OutputTexture->Filter = SourceTexture->Filter;
    OutputTexture->AddressX = SourceTexture->AddressX;
    OutputTexture->AddressY = SourceTexture->AddressY;
    OutputTexture->NeverStream = SourceTexture->NeverStream;
}

static void AddAlphaToSelectedTexture(const FToolMenuContext& MenuContext)
{
    const TArray<UTexture2D*> Textures = GetSelectedTextures(MenuContext);
    if (Textures.Num() != 2)
    {
        ShowMessage(LOCTEXT("NeedTwoTextures", "Select exactly two Texture2D assets in this order: Base color/RGB texture first, Alpha mask second."), LOCTEXT("InvalidAlphaSelectionTitle", "RMA Channel Packer"));
        return;
    }

    TArray64<uint8> BasePixels;
    TArray64<uint8> AlphaPixels;
    int32 BaseWidth = 0;
    int32 BaseHeight = 0;
    int32 AlphaWidth = 0;
    int32 AlphaHeight = 0;
    FString Error;

    if (!ReadTexturePixels(Textures[0], BasePixels, BaseWidth, BaseHeight, Error) || !ReadTexturePixels(Textures[1], AlphaPixels, AlphaWidth, AlphaHeight, Error))
    {
        ShowMessage(FText::FromString(Error), LOCTEXT("AlphaReadFailedTitle", "RMA Channel Packer"));
        return;
    }

    if (BaseWidth != AlphaWidth || BaseHeight != AlphaHeight)
    {
        ShowMessage(LOCTEXT("AlphaResolutionMismatch", "Selected textures have different resolutions. The alpha texture will be resized to match the first selected texture."), LOCTEXT("ResolutionMismatchTitle", "RMA Channel Packer"));
    }

    TArray64<uint8> OutputPixels;
    const int64 PixelCount = static_cast<int64>(BaseWidth) * BaseHeight;
    OutputPixels.SetNumUninitialized(PixelCount * ChannelCount);
    for (int32 Y = 0; Y < BaseHeight; ++Y)
    {
        for (int32 X = 0; X < BaseWidth; ++X)
        {
            const int64 PixelIndex = static_cast<int64>(Y) * BaseWidth + X;
            OutputPixels[PixelIndex * 4 + 0] = BasePixels[PixelIndex * 4 + 2];
            OutputPixels[PixelIndex * 4 + 1] = BasePixels[PixelIndex * 4 + 1];
            OutputPixels[PixelIndex * 4 + 2] = BasePixels[PixelIndex * 4 + 0];
            OutputPixels[PixelIndex * 4 + 3] = SampleRedChannelNearest(AlphaPixels, AlphaWidth, AlphaHeight, X, Y, BaseWidth, BaseHeight);
        }
    }

    FString OutputPackagePath = BuildAlphaOutputPackagePath(Textures[0]);
    FString OutputAssetName = FPackageName::GetLongPackageAssetName(OutputPackagePath);
    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    AssetToolsModule.Get().CreateUniqueAssetName(OutputPackagePath, TEXT(""), OutputPackagePath, OutputAssetName);

    UPackage* Package = CreatePackage(*OutputPackagePath);
    UTexture2D* OutputTexture = NewObject<UTexture2D>(Package, *OutputAssetName, RF_Public | RF_Standalone | RF_Transactional);
    OutputTexture->Source.Init(BaseWidth, BaseHeight, 1, 1, TSF_BGRA8, OutputPixels.GetData());
    CopyTextureSettings(Textures[0], OutputTexture);
    OutputTexture->PostEditChange();

    FAssetRegistryModule::AssetCreated(OutputTexture);
    Package->MarkPackageDirty();

    ShowMessage(FText::Format(LOCTEXT("AlphaComplete", "Created {0}\n\nRGB = first selected texture\nA = second selected texture"), FText::FromString(OutputPackagePath)), LOCTEXT("AlphaCompleteTitle", "RMA Channel Packer"));
}

static void PackSelectedTextures(const FToolMenuContext& MenuContext)
{
    const TArray<UTexture2D*> Textures = GetSelectedTextures(MenuContext);
    if (Textures.Num() != 3)
    {
        ShowMessage(LOCTEXT("NeedThreeTextures", "Select exactly three Texture2D assets in this order: Roughness, Metallic, Ambient Occlusion."), LOCTEXT("InvalidSelectionTitle", "RMA Channel Packer"));
        return;
    }

    TArray64<uint8> SourcePixels[3];
    int32 Widths[3] = {0, 0, 0};
    int32 Heights[3] = {0, 0, 0};
    FString Error;

    for (int32 Index = 0; Index < 3; ++Index)
    {
        if (!ReadTexturePixels(Textures[Index], SourcePixels[Index], Widths[Index], Heights[Index], Error))
        {
            ShowMessage(FText::FromString(Error), LOCTEXT("ReadFailedTitle", "RMA Channel Packer"));
            return;
        }
    }

    if (Widths[0] != Widths[1] || Widths[0] != Widths[2] || Heights[0] != Heights[1] || Heights[0] != Heights[2])
    {
        ShowMessage(LOCTEXT("ResolutionMismatch", "Selected textures have different resolutions. Metallic and AO will be resized to match the first selected Roughness texture."), LOCTEXT("ResolutionMismatchTitle", "RMA Channel Packer"));
    }

    TArray64<uint8> PackedPixels;
    const int64 PixelCount = static_cast<int64>(Widths[0]) * Heights[0];
    PackedPixels.SetNumUninitialized(PixelCount * ChannelCount);
    for (int32 Y = 0; Y < Heights[0]; ++Y)
    {
        for (int32 X = 0; X < Widths[0]; ++X)
        {
            const int64 PixelIndex = static_cast<int64>(Y) * Widths[0] + X;
            PackedPixels[PixelIndex * 4 + 0] = SampleRedChannelNearest(SourcePixels[2], Widths[2], Heights[2], X, Y, Widths[0], Heights[0]);
            PackedPixels[PixelIndex * 4 + 1] = SampleRedChannelNearest(SourcePixels[1], Widths[1], Heights[1], X, Y, Widths[0], Heights[0]);
            PackedPixels[PixelIndex * 4 + 2] = SourcePixels[0][PixelIndex * 4 + 0];
            PackedPixels[PixelIndex * 4 + 3] = 255;
        }
    }

    FString OutputPackagePath = BuildOutputPackagePath(Textures[0]);
    FString OutputAssetName = FPackageName::GetLongPackageAssetName(OutputPackagePath);
    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    AssetToolsModule.Get().CreateUniqueAssetName(OutputPackagePath, TEXT(""), OutputPackagePath, OutputAssetName);

    UPackage* Package = CreatePackage(*OutputPackagePath);
    UTexture2D* OutputTexture = NewObject<UTexture2D>(Package, *OutputAssetName, RF_Public | RF_Standalone | RF_Transactional);
    OutputTexture->Source.Init(Widths[0], Heights[0], 1, 1, TSF_BGRA8, PackedPixels.GetData());
    OutputTexture->CompressionSettings = TC_Masks;
    OutputTexture->SRGB = false;
    OutputTexture->MipGenSettings = TMGS_NoMipmaps;
    OutputTexture->LODGroup = TEXTUREGROUP_World;
    OutputTexture->PostEditChange();

    FAssetRegistryModule::AssetCreated(OutputTexture);
    Package->MarkPackageDirty();

    ShowMessage(FText::Format(LOCTEXT("PackingComplete", "Created {0}\n\nR = Roughness\nG = Metallic\nB = Ambient Occlusion"), FText::FromString(OutputPackagePath)), LOCTEXT("PackingCompleteTitle", "RMA Channel Packer"));
}
}

void FRMAChannelPackerEditorModule::StartupModule()
{
    UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FRMAChannelPackerEditorModule::RegisterMenus));
}

void FRMAChannelPackerEditorModule::ShutdownModule()
{
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);
}

void FRMAChannelPackerEditorModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);
    UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("ContentBrowser.AssetContextMenu.Texture2D"));
    FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("GetAssetActions"));
    Section.AddMenuEntry(
        TEXT("PackRMAChannels"),
        LOCTEXT("PackRMAChannelsLabel", "Create RMA Texture"),
        LOCTEXT("PackRMAChannelsTooltip", "Select three Texture2D assets in order: Roughness, Metallic, Ambient Occlusion. Creates a new _RMA texture in Unreal Editor."),
        FSlateIcon(),
        FToolMenuExecuteAction::CreateStatic(&RMAChannelPacker::PackSelectedTextures));

    Section.AddMenuEntry(
        TEXT("AddAlphaChannel"),
        LOCTEXT("AddAlphaChannelLabel", "Create Texture With Alpha"),
        LOCTEXT("AddAlphaChannelTooltip", "Select two Texture2D assets in order: RGB/base texture first, alpha texture second. Creates a new texture that preserves the first texture settings and uses the second texture as alpha."),
        FSlateIcon(),
        FToolMenuExecuteAction::CreateStatic(&RMAChannelPacker::AddAlphaToSelectedTexture));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRMAChannelPackerEditorModule, RMAChannelPackerEditor)
