#include "FlyingScenarioSelectionWidget.h"

#include "FlyingCoreSimComponent.h"
#include "flying/core_sim/scenario.hpp"

#include <string>
#include <vector>

namespace {

FString ToFString(const std::string& Value)
{
  return FString(UTF8_TO_TCHAR(Value.c_str()));
}

FFlyingScenarioLocation ToUnreal(const flying::core_sim::PilotScenarioLocation& Value)
{
  FFlyingScenarioLocation Result;
  Result.LocationId = FName(*ToFString(Value.location_id));
  Result.AerodromeId = ToFString(Value.aerodrome_id);
  Result.RunwayEndId = ToFString(Value.runway_end_id);
  Result.DisplayName = ToFString(Value.display_name);
  Result.LatitudeDegrees = Value.latitude_deg;
  Result.LongitudeDegrees = Value.longitude_deg;
  Result.ElevationMeters = Value.elevation_m;
  Result.TrueHeadingDegrees = Value.true_heading_deg;
  Result.bSelectable = Value.selectable;
  return Result;
}

} // namespace

void UFlyingScenarioSelectionWidget::NativeConstruct()
{
  Super::NativeConstruct();
  EnsurePilotLocations();
}

void UFlyingScenarioSelectionWidget::RefreshPilotLocations()
{
  PilotLocations.Reset();
  if (SelectedScenario.LocationId.IsNone())
  {
    SelectedScenario.LocationId = FName(TEXT("FPPV-RWY-09"));
  }

  const std::vector<flying::core_sim::PilotScenarioLocation> Locations =
    flying::core_sim::default_pilot_scenario_locations();
  PilotLocations.Reserve(static_cast<int32>(Locations.size()));
  for (const flying::core_sim::PilotScenarioLocation& Location : Locations)
  {
    PilotLocations.Add(ToUnreal(Location));
  }
}

void UFlyingScenarioSelectionWidget::EnsurePilotLocations()
{
  if (PilotLocations.Num() == 0)
  {
    RefreshPilotLocations();
  }
}

bool UFlyingScenarioSelectionWidget::SelectScenario(
  FName LocationId,
  EFlyingScenarioStartMode StartMode)
{
  EnsurePilotLocations();

  const FFlyingScenarioLocation* Location = PilotLocations.FindByPredicate(
    [LocationId](const FFlyingScenarioLocation& Candidate)
    {
      return Candidate.LocationId == LocationId && Candidate.bSelectable;
    });
  if (!Location)
  {
    return false;
  }

  SelectedScenario.LocationId = LocationId;
  SelectedScenario.StartMode = StartMode;
  return true;
}

bool UFlyingScenarioSelectionWidget::StartSelectedScenario(
  UFlyingCoreSimComponent* CoreSimComponent)
{
  EnsurePilotLocations();
  return CoreSimComponent ? CoreSimComponent->StartScenario(SelectedScenario) : false;
}
