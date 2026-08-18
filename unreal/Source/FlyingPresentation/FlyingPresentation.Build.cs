using System.IO;
using UnrealBuildTool;

public class FlyingPresentation : ModuleRules
{
    public FlyingPresentation(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        bEnableExceptions = true;
        bUseUnity = false;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "FlyingCoreSimBridge",
            "CesiumRuntime",
            "ProceduralMeshComponent",
            "UMG"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Json",
            "JsonUtilities",
            "Projects",
            "SQLiteCore"
        });

        string RepositoryRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", ".."));
        PrivateIncludePaths.Add(Path.Combine(RepositoryRoot, "core_sim", "include"));
        PrivateIncludePaths.Add(Path.Combine(RepositoryRoot, "geo_terrain", "include"));

        PrivateDefinitions.Add("FLYING_PRESENTATION_OFFLINE_ONLY=1");
    }
}
