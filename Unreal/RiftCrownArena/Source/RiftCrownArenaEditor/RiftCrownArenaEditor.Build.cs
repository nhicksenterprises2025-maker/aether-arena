using UnrealBuildTool;
public class RiftCrownArenaEditor : ModuleRules
{
    public RiftCrownArenaEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "UnrealEd", "AssetTools",
            "AssetRegistry", "RiftCrownArena", "Niagara", "NiagaraEditor",
            "Json", "JsonUtilities", "Slate", "SlateCore", "SlateNullRenderer"
        });
    }
}
