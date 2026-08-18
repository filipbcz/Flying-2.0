#include "FlyingCoreSimBridge.h"

#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FFlyingCoreSimBridgeModule"

FFlyingCoreSimBridgeModule& FFlyingCoreSimBridgeModule::Get()
{
  return FModuleManager::LoadModuleChecked<FFlyingCoreSimBridgeModule>("FlyingCoreSimBridge");
}

bool FFlyingCoreSimBridgeModule::IsAvailable()
{
  return FModuleManager::Get().IsModuleLoaded("FlyingCoreSimBridge");
}

void FFlyingCoreSimBridgeModule::StartupModule()
{
}

void FFlyingCoreSimBridgeModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FFlyingCoreSimBridgeModule, FlyingCoreSimBridge)
