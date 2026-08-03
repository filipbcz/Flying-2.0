using UnrealBuildTool;

public class FlyingEditorTarget : TargetRules
{
    public FlyingEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("FlyingPresentation");
    }
}
