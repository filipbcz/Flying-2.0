using UnrealBuildTool;

public class FlyingEditorTarget : TargetRules
{
    public FlyingEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("FlyingPresentation");
    }
}
