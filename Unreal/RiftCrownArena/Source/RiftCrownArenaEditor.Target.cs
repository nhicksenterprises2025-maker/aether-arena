using UnrealBuildTool;
public class RiftCrownArenaEditorTarget : TargetRules
{
    public RiftCrownArenaEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.AddRange(new string[] { "RiftCrownArena", "RiftCrownArenaEditor" });
    }
}
