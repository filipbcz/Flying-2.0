using UnrealBuildTool;

public class FlyingTarget : TargetRules
{
    public FlyingTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("FlyingPresentation");
    }
}
