#include "RMAChannelPackerEditorModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "ContentBrowserMenuContexts.h"
#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"
#include "Misc/MessageDialog.h"
#include "Math/Float16.h"
#include "Modules/ModuleManager.h"
#include "TextureCompiler.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "RMAChannelPackerEditor"

namespace RMAChannelPacker
{
static constexpr int32 ChannelCount = 4;

static void ShowMessage(const FText& Message, const FText& Title)
{
    FMessageDialog::Open(EAppMsgType::Ok, Message, Title);
}

static TArray<UTexture2D*> GetSelectedTextures(const FToolMenuContext& MenuContext)
{
    TArray<UTexture2D*> Textures;
    if (const UContentBrowserAssetContextMenuContext* Context = MenuContext.FindContext<UContentBrowserAssetContextMenuContext>())
    {
        for (const FAssetData& Asset : Context->SelectedAssets)
        {
            if (UTexture2D* Texture = Cast<UTexture2D>(Asset.GetAsset()))
            {
                Textures.Add(Texture);
            }
        }
    }
    return Textures;
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

static FString BuildOutputPackagePath(UTexture2D* RoughnessTexture)
{
    FString PackagePath = RoughnessTexture->GetOutermost()->GetName();
    FString Path;
    FString Name;
    PackagePath.Split(TEXT("/"), &Path, &Name, ESearchCase::IgnoreCase, ESearchDir::FromEnd);

    const FString Suffixes[] = {TEXT("_Roughness"), TEXT("_roughness"), TEXT("_Rough"), TEXT("_rough"), TEXT("_R"), TEXT("_r")};
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
        ShowMessage(LOCTEXT("ResolutionMismatch", "All three selected textures must have the same resolution."), LOCTEXT("PackingFailedTitle", "RMA Channel Packer"));
        return;
    }

    TArray64<uint8> PackedPixels;
    const int64 PixelCount = static_cast<int64>(Widths[0]) * Heights[0];
    PackedPixels.SetNumUninitialized(PixelCount * ChannelCount);
    for (int64 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
    {
        PackedPixels[PixelIndex * 4 + 0] = SourcePixels[2][PixelIndex * 4 + 0];
        PackedPixels[PixelIndex * 4 + 1] = SourcePixels[1][PixelIndex * 4 + 0];
        PackedPixels[PixelIndex * 4 + 2] = SourcePixels[0][PixelIndex * 4 + 0];
        PackedPixels[PixelIndex * 4 + 3] = 255;
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
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRMAChannelPackerEditorModule, RMAChannelPackerEditor)
