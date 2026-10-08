using UnrealBuildTool;
public class RiftCrownArena : ModuleRules
{
    public RiftCrownArena(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        bUseUnity = false;
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
            "UMG", "CommonUI", "Slate", "SlateCore", "Niagara", "Json",
            "JsonUtilities", "RenderCore", "RHI", "DeveloperSettings", "Projects"
        });
    }
}
