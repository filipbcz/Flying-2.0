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
            "CesiumRuntime",
            "ProceduralMeshComponent",
            "UMG"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Json",
            "JsonUtilities",
            "Projects"
        });

        string RepositoryRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", ".."));
        PublicIncludePaths.Add(Path.Combine(RepositoryRoot, "core_sim", "include"));
        PublicIncludePaths.Add(Path.Combine(RepositoryRoot, "geo_terrain", "include"));

        PrivateDefinitions.Add("FLYING_PRESENTATION_OFFLINE_ONLY=1");
    }
}
