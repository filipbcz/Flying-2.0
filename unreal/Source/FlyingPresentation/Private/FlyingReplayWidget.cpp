#include "FlyingReplayWidget.h"

#include "FlyingCoreSimComponent.h"

bool UFlyingReplayWidget::StartRecording(
  UFlyingCoreSimComponent* CoreSimComponent,
  const FString& SessionId)
{
  const bool bResult =
    CoreSimComponent && CoreSimComponent->StartTelemetryRecording(TelemetryPath, SessionId);
  SyncStatus(CoreSimComponent);
  return bResult;
}

bool UFlyingReplayWidget::StopRecording(UFlyingCoreSimComponent* CoreSimComponent)
{
  const bool bResult = CoreSimComponent && CoreSimComponent->StopTelemetryRecording();
  SyncStatus(CoreSimComponent);
  return bResult;
}

bool UFlyingReplayWidget::LoadReplay(
  UFlyingCoreSimComponent* CoreSimComponent,
  bool bWarnOnIncompatible)
{
  const bool bResult =
    CoreSimComponent && CoreSimComponent->LoadTelemetryReplay(ReplayPath, bWarnOnIncompatible);
  SyncStatus(CoreSimComponent);
  return bResult;
}

bool UFlyingReplayWidget::PlayReplay(
  UFlyingCoreSimComponent* CoreSimComponent,
  bool bWarnOnIncompatible)
{
  const bool bResult =
    CoreSimComponent && CoreSimComponent->PlayLoadedTelemetryReplay(bWarnOnIncompatible);
  SyncStatus(CoreSimComponent);
  return bResult;
}

bool UFlyingReplayWidget::ExportCsv(UFlyingCoreSimComponent* CoreSimComponent)
{
  const bool bResult =
    CoreSimComponent && CoreSimComponent->ExportTelemetryCsv(CsvExportPath);
  SyncStatus(CoreSimComponent);
  return bResult;
}

bool UFlyingReplayWidget::ExportJson(UFlyingCoreSimComponent* CoreSimComponent)
{
  const bool bResult =
    CoreSimComponent && CoreSimComponent->ExportTelemetryJson(JsonExportPath);
  SyncStatus(CoreSimComponent);
  return bResult;
}

void UFlyingReplayWidget::SyncStatus(const UFlyingCoreSimComponent* CoreSimComponent)
{
  LastStatus = CoreSimComponent ? CoreSimComponent->LastTelemetryStatus : FString();
}
