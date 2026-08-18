using System.IO;
using UnrealBuildTool;

public class FlyingCoreSimBridge : ModuleRules
{
    public FlyingCoreSimBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        bEnableExceptions = true;
        bUseUnity = false;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "CoreUObject",
            "Engine",
            "Json",
            "JsonUtilities",
            "Projects"
        });

        string RepositoryRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", ".."));
        PublicIncludePaths.Add(Path.Combine(RepositoryRoot, "core_sim", "include"));
        PublicIncludePaths.Add(Path.Combine(RepositoryRoot, "geo_terrain", "include"));

        PublicDefinitions.Add("FLYING_CORE_SIM_VERSION=\"0.1.0\"");
        PublicDefinitions.Add("FLYING_CORE_SIM_AIRCRAFT_CONFIG_DIR=\"core_sim/aircraft\"");
    }
}
