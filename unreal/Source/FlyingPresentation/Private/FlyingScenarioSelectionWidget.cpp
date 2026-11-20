#include "FlyingScenarioSelectionWidget.h"
#include "FlyingCoreSimComponent.h"
#include "FlyingInstalledPackageLibrary.h"
#include "flying/core_sim/selected_package.hpp"

void UFlyingScenarioSelectionWidget::NativeConstruct()
{
  Super::NativeConstruct();
  RefreshPilotLocations();
}
void UFlyingScenarioSelectionWidget::RefreshPilotLocations()
{
  PilotLocations = UFlyingInstalledPackageLibrary::GetPackageLocations(LastStatus);
  SelectedScenario = {};
  CatalogGeneration = flying::core_sim::selected_package_runtime().generation();
}
void UFlyingScenarioSelectionWidget::EnsurePilotLocations()
{
  if (CatalogGeneration != flying::core_sim::selected_package_runtime().generation() || PilotLocations.IsEmpty())
    RefreshPilotLocations();
}
bool UFlyingScenarioSelectionWidget::SelectScenario(FName LocationId, EFlyingScenarioStartMode StartMode)
{
  EnsurePilotLocations();
  FFlyingScenarioSelection Candidate;
  Candidate.LocationId = LocationId;
  Candidate.StartMode = StartMode;
  if (!UFlyingInstalledPackageLibrary::BindSelection(Candidate, LastStatus)) return false;
  SelectedScenario = Candidate;
  return true;
}
bool UFlyingScenarioSelectionWidget::StartSelectedScenario(UFlyingCoreSimComponent* CoreSimComponent)
{
  if (SelectedScenario.PackageGeneration != static_cast<int64>(flying::core_sim::selected_package_runtime().generation()))
  {
    LastStatus = TEXT("Stale package selection; refresh locations and select a start again");
    return false;
  }
  const bool Started = CoreSimComponent && CoreSimComponent->StartScenario(SelectedScenario);
  LastStatus = Started ? TEXT("Scenario started") : TEXT("Scenario rejected; inspect CoreSim diagnostics for installed package or aircraft failure");
  return Started;
}
