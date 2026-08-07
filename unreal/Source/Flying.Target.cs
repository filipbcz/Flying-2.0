using UnrealBuildTool;

public class FlyingTarget : TargetRules
{
    public FlyingTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        bUseCrashReportClient = true;
        ExtraModuleNames.Add("FlyingPresentation");
    }
}
