using UnrealBuildTool;
public class DesertBuildingLabEditor : ModuleRules
{
    public DesertBuildingLabEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] {"Core", "CoreUObject", "Engine", "DesertBuildingLab"});
        PrivateDependencyModuleNames.AddRange(new[] {
            "UnrealEd", "ToolMenus", "Slate", "SlateCore", "InputCore", "RenderCore",
            "AdvancedPreviewScene", "ContentBrowser", "AssetRegistry", "AssetTools", "PhysicsCore", "PropertyEditor", "Kismet"
        });
    }
}
