using UnrealBuildTool;
using System.Collections.Generic;
public class RiftCrownArenaTarget : TargetRules
{
    public RiftCrownArenaTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("RiftCrownArena");
        // Shipping diagnostics use FRiftDiagnostics; keep the installed engine's binary logging ABI.
    }
}
