#include "FlyingInstalledPackageLibrary.h"
#include "flying/core_sim/selected_package.hpp"
#include "Misc/Paths.h"

bool UFlyingInstalledPackageLibrary::SelectInstalledPackage(const FString& DataRoot, const FString& RelativePackage, const FString& ManifestSha256, FString& Diagnostic)
{
  try
  {
    flying::core_sim::selected_package_runtime().select(
      std::filesystem::path(*FPaths::ConvertRelativePathToFull(DataRoot)),
      std::filesystem::path(*RelativePackage), TCHAR_TO_UTF8(*ManifestSha256));
    Diagnostic = TEXT("Installed regional package validated");
    return true;
  }
  catch (const std::exception& Error)
  {
    Diagnostic = UTF8_TO_TCHAR(Error.what());
    return false;
  }
}
TArray<FFlyingScenarioLocation> UFlyingInstalledPackageLibrary::GetPackageLocations(FString& Diagnostic)
{
  TArray<FFlyingScenarioLocation> Result;
  try
  {
    auto& Runtime = flying::core_sim::selected_package_runtime();
    Runtime.revalidate();
    for (const auto& Value : Runtime.current().locations)
    {
      FFlyingScenarioLocation Location;
      Location.LocationId = FName(UTF8_TO_TCHAR(Value.location_id.c_str()));
      Location.AerodromeId = UTF8_TO_TCHAR(Value.aerodrome_id.c_str());
      Location.RunwayId = UTF8_TO_TCHAR(Value.runway_id.c_str());
      Location.RunwayEndId = UTF8_TO_TCHAR(Value.runway_end_id.c_str());
      Location.DisplayName = UTF8_TO_TCHAR(Value.display_name.c_str());
      Location.LatitudeDegrees = Value.latitude_deg;
      Location.LongitudeDegrees = Value.longitude_deg;
      Location.ElevationMeters = Value.elevation_m;
      Location.TrueHeadingDegrees = Value.true_heading_deg;
      Location.bSelectable = Value.selectable;
      Location.bGroundEligible = Value.ground_eligible;
      Location.Diagnostic = UTF8_TO_TCHAR(Value.diagnostic.c_str());
      Result.Add(Location);
    }
    Diagnostic = TEXT("Package locations loaded; ground eligibility is checked separately from airborne eligibility");
  }
  catch (const std::exception& Error) { Diagnostic = UTF8_TO_TCHAR(Error.what()); }
  return Result;
}
bool UFlyingInstalledPackageLibrary::BindSelection(FFlyingScenarioSelection& Selection, FString& Diagnostic)
{
  try
  {
    auto& Runtime = flying::core_sim::selected_package_runtime();
    const auto Request = Runtime.selection(TCHAR_TO_UTF8(*Selection.LocationId.ToString()),
      static_cast<flying::core_sim::ScenarioStartMode>(Selection.StartMode));
    (void)Runtime.start(Request, true);
    Selection.PackageGeneration = static_cast<int64>(Request.package_generation);
    Selection.PackageManifestSha256 = UTF8_TO_TCHAR(Runtime.current().manifest_sha256.c_str());
    Diagnostic = TEXT("Scenario bound to selected installed package");
    return true;
  }
  catch (const std::exception& Error) { Diagnostic = UTF8_TO_TCHAR(Error.what()); return false; }
}
