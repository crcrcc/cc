using UnrealBuildTool;

public class RMAChannelPackerEditor : ModuleRules
{
    public RMAChannelPackerEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AssetRegistry",
            "AssetTools",
            "ContentBrowser",
            "Core",
            "CoreUObject",
            "EditorFramework",
            "Engine",
            "ImageCore",
            "ImageWrapper",
            "Slate",
            "SlateCore",
            "ToolMenus",
            "UnrealEd"
        });
    }
}
