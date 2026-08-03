#include "FlyingInputCalibrationWidget.h"

namespace {

bool SameSource(const FFlyingPhysicalInputBinding& A, const FFlyingPhysicalInputBinding& B)
{
  return A.DeviceClass == B.DeviceClass &&
         A.DeviceId == B.DeviceId &&
         A.ControlPath == B.ControlPath;
}

} // namespace

bool UFlyingInputCalibrationWidget::SelectAxisBinding(
  EFlyingFlightControlAxis Axis,
  const FFlyingPhysicalInputBinding& Source)
{
  for (const FFlyingAxisBinding& Binding : EditingProfile.AxisBindings)
  {
    if (Binding.Axis == Axis && SameSource(Binding.Source, Source))
    {
      SelectedAxisBinding = Binding;
      return true;
    }
  }

  return false;
}

void UFlyingInputCalibrationWidget::SetSelectedCalibration(
  const FFlyingAxisCalibration& Calibration)
{
  SelectedAxisBinding.Calibration = Calibration;
}

bool UFlyingInputCalibrationWidget::CommitSelectedCalibration()
{
  return AddOrReplaceAxisBinding(SelectedAxisBinding);
}

bool UFlyingInputCalibrationWidget::AddOrReplaceAxisBinding(
  const FFlyingAxisBinding& Binding)
{
  for (FFlyingAxisBinding& Existing : EditingProfile.AxisBindings)
  {
    if (Existing.Axis == Binding.Axis && SameSource(Existing.Source, Binding.Source))
    {
      Existing = Binding;
      return true;
    }
  }

  EditingProfile.AxisBindings.Add(Binding);
  return true;
}
