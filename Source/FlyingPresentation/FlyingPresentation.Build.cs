using System.IO;
using UnrealBuildTool;

public class FlyingPresentation : ModuleRules
{
    public FlyingPresentation(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        bUseUnity = false;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "CesiumRuntime",
            "DeveloperSettings"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Projects"
        });

        PublicDefinitions.Add("FLYING_PRESENTATION_OFFLINE_ONLY=1");

        PublicIncludePaths.AddRange(new[]
        {
            Path.Combine(ModuleDirectory, "..", "GeoTerrain", "Public"),
            Path.Combine(ModuleDirectory, "..", "..", "geo_terrain", "include")
        });
    }
}
