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
            "JsonUtilities", "RenderCore", "RHI", "DeveloperSettings", "Projects", "AudioMixer"
        });
        // The bundled typeface is OFL licensed. Keep its complete copyright
        // notice and license directly readable beside the cooked game assets.
        RuntimeDependencies.Add("$(ProjectDir)/Content/Rift/Fonts/Barlow-OFL.txt", StagedFileType.NonUFS);
    }
}
