#pragma once

#include "Modules/ModuleManager.h"

class FFlyingPresentationModule final : public IModuleInterface
{
public:
  void StartupModule() override;
  void ShutdownModule() override;
};
