#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FlyingCoreSimBridge.h"
#include "FlyingCoreSimStateSnapshot.h"
#include "FlyingInputMappingTypes.h"

#include "FlyingCoreSimAircraftActor.generated.h"

class UFlyingCesiumGeoreferenceComponent;
class UFlyingCoreSimComponent;
class UAudioComponent;
class UCameraComponent;
class UPointLightComponent;
class USoundWaveProcedural;
class USpringArmComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

UENUM(BlueprintType)
enum class EFlyingCockpitCameraMode : uint8
{
  Pilot UMETA(DisplayName="Pilot"),
  Instruments UMETA(DisplayName="Instruments"),
  ExteriorOrbit UMETA(DisplayName="Exterior Orbit"),
  ReplayInspection UMETA(DisplayName="Replay Inspection")
};

UENUM(BlueprintType)
enum class EFlyingCockpitControl : uint8
{
  BatteryMaster UMETA(DisplayName="Battery Master"),
  Alternator UMETA(DisplayName="Alternator"),
  AvionicsMaster UMETA(DisplayName="Avionics Master"),
  FuelPump UMETA(DisplayName="Fuel Pump"),
  PitotHeat UMETA(DisplayName="Pitot Heat"),
  StandbyVacuum UMETA(DisplayName="Standby Vacuum"),
  Magnetos UMETA(DisplayName="Magnetos"),
  Starter UMETA(DisplayName="Starter"),
  Throttle UMETA(DisplayName="Throttle"),
  Mixture UMETA(DisplayName="Mixture"),
  Propeller UMETA(DisplayName="Propeller"),
  FuelSelector UMETA(DisplayName="Fuel Selector"),
  ParkingBrake UMETA(DisplayName="Parking Brake"),
  Flaps UMETA(DisplayName="Flaps"),
  Trim UMETA(DisplayName="Trim"),
  EmergencyFuelCutoff UMETA(DisplayName="Emergency Fuel Cutoff"),
  FireExtinguisher UMETA(DisplayName="Fire Extinguisher"),
  ReplayScrub UMETA(DisplayName="Replay Scrub")
};

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingCockpitControlState
{
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Cockpit")
  EFlyingCockpitControl Control = EFlyingCockpitControl::BatteryMaster;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Cockpit")
  FString Label;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Cockpit")
  double PositionNorm = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Cockpit")
  bool bEngaged = false;
};

UCLASS()
class FLYINGPRESENTATION_API AFlyingCoreSimAircraftActor : public AActor
{
  GENERATED_BODY()

public:
  AFlyingCoreSimAircraftActor();

  void BeginPlay() override;
  void Tick(float DeltaSeconds) override;
  void ApplyWorldOffset(const FVector& InOffset, bool bWorldShift) override;

  UFUNCTION(BlueprintCallable, Category="Flying|CoreSim")
  void UpdatePresentationFromCoreSim();

  UFUNCTION(BlueprintCallable, Category="Flying|CoreSim")
  void UpdatePresentationFromSnapshot(const FFlyingCoreSimStateSnapshot& Snapshot);

  void UpdatePresentationFromImmutableSnapshot(
    const FFlyingCoreSimImmutableStateSnapshot& Snapshot);

  UFUNCTION(BlueprintPure, Category="Flying|CoreSim")
  bool DoCockpitAndExternalVisualsShareAuthoritativeSnapshotRoot() const;

  bool DoesVisualPresentationMatchImmutableSnapshot(
    const FFlyingCoreSimImmutableStateSnapshot& Snapshot) const;

  UFUNCTION(BlueprintCallable, Category="Flying|Cockpit")
  void InteractCockpitControl(EFlyingCockpitControl Control, double PositionNorm, bool bEngaged);

  UFUNCTION(BlueprintCallable, Category="Flying|Cockpit")
  void SetCockpitNightLighting(bool bEnabled);

  UFUNCTION(BlueprintCallable, Category="Flying|Cockpit")
  void SetCockpitCameraMode(EFlyingCockpitCameraMode Mode);

  UFUNCTION(BlueprintPure, Category="Flying|Cockpit")
  TArray<FFlyingCockpitControlState> GetCockpitControls() const;

  UFUNCTION(BlueprintPure, Category="Flying|Cockpit")
  EFlyingCockpitCameraMode GetCockpitCameraMode() const;

  UFUNCTION(BlueprintPure, Category="Flying|Cockpit")
  UCameraComponent* GetActiveCameraComponent() const;

private:
  void BuildCockpitControls();
  void BuildInstrumentPanel();
  UStaticMeshComponent* AddPrimitiveMesh(FName Name,
                                         const TCHAR* MeshPath,
                                         USceneComponent* Parent,
                                         FVector RelativeLocation,
                                         FVector RelativeScale,
                                         FRotator RelativeRotation,
                                         FLinearColor Color);
  UTextRenderComponent* AddLabel(FName Name,
                                 USceneComponent* Parent,
                                 const FString& Text,
                                 FVector RelativeLocation,
                                 FRotator RelativeRotation,
                                 float Size,
                                 FLinearColor Color);
  FFlyingCockpitControlState* FindControlState(EFlyingCockpitControl Control);
  const FFlyingCockpitControlState* FindControlState(EFlyingCockpitControl Control) const;
  FFlyingMappedInputState BuildMappedInputStateFromCockpit() const;
  bool IsCockpitEngineRunning(const FFlyingAircraftInstrumentSnapshot& Instruments) const;
  void UpdateCockpitFromInstruments(const FFlyingAircraftInstrumentSnapshot& Instruments);
  void UpdateLighting(const FFlyingAircraftInstrumentSnapshot& Instruments);
  void UpdateAudio(const FFlyingAircraftInstrumentSnapshot& Instruments);
  void QueueProceduralAudio(float DeltaSeconds);
  void UpdateCamera();

  UPROPERTY(VisibleAnywhere, Category="Flying|Components")
  TObjectPtr<USceneComponent> SceneRoot;

  UPROPERTY(VisibleAnywhere, Category="Flying|Components")
  TObjectPtr<UStaticMeshComponent> AircraftMesh;

  UPROPERTY(VisibleAnywhere, Category="Flying|Components")
  TObjectPtr<USceneComponent> CockpitRoot;

  UPROPERTY(VisibleAnywhere, Category="Flying|Components")
  TObjectPtr<UStaticMeshComponent> InstrumentPanelMesh;

  UPROPERTY(VisibleAnywhere, Category="Flying|Components")
  TObjectPtr<UStaticMeshComponent> LeftWingMesh;

  UPROPERTY(VisibleAnywhere, Category="Flying|Components")
  TObjectPtr<UStaticMeshComponent> RightWingMesh;

  UPROPERTY(VisibleAnywhere, Category="Flying|Components")
  TObjectPtr<UStaticMeshComponent> TailMesh;

  UPROPERTY(VisibleAnywhere, Category="Flying|Components")
  TObjectPtr<UStaticMeshComponent> PropellerMesh;

  UPROPERTY(VisibleAnywhere, Category="Flying|Components")
  TObjectPtr<UPointLightComponent> CockpitFloodLight;

  UPROPERTY(VisibleAnywhere, Category="Flying|Components")
  TObjectPtr<UPointLightComponent> InstrumentBacklight;

  UPROPERTY(VisibleAnywhere, Category="Flying|Components")
  TObjectPtr<UCameraComponent> CockpitCamera;

  UPROPERTY(VisibleAnywhere, Category="Flying|Components")
  TObjectPtr<UCameraComponent> InstrumentCamera;

  UPROPERTY(VisibleAnywhere, Category="Flying|Components")
  TObjectPtr<USpringArmComponent> ExteriorSpringArm;

  UPROPERTY(VisibleAnywhere, Category="Flying|Components")
  TObjectPtr<UCameraComponent> ExteriorCamera;

  UPROPERTY(VisibleAnywhere, Category="Flying|Components")
  TObjectPtr<UCameraComponent> ReplayInspectionCamera;

  UPROPERTY(VisibleAnywhere, Category="Flying|Audio")
  TObjectPtr<UAudioComponent> EngineAudio;

  UPROPERTY(VisibleAnywhere, Category="Flying|Audio")
  TObjectPtr<UAudioComponent> PropellerAudio;

  UPROPERTY(VisibleAnywhere, Category="Flying|Audio")
  TObjectPtr<UAudioComponent> CabinAudio;

  UPROPERTY(VisibleAnywhere, Category="Flying|Audio")
  TObjectPtr<UAudioComponent> AirflowAudio;

  UPROPERTY(VisibleAnywhere, Category="Flying|Audio")
  TObjectPtr<UAudioComponent> DamageAudio;

  UPROPERTY(VisibleAnywhere, Category="Flying|Audio")
  TObjectPtr<USoundWaveProcedural> EngineSound;

  UPROPERTY(VisibleAnywhere, Category="Flying|Audio")
  TObjectPtr<USoundWaveProcedural> PropellerSound;

  UPROPERTY(VisibleAnywhere, Category="Flying|Audio")
  TObjectPtr<USoundWaveProcedural> CabinSound;

  UPROPERTY(VisibleAnywhere, Category="Flying|Audio")
  TObjectPtr<USoundWaveProcedural> AirflowSound;

  UPROPERTY(VisibleAnywhere, Category="Flying|Audio")
  TObjectPtr<USoundWaveProcedural> DamageSound;

  double EngineTonePhaseRadians = 0.0;
  double PropellerTonePhaseRadians = 0.0;
  double CabinTonePhaseRadians = 0.0;
  double AirflowTonePhaseRadians = 0.0;
  double DamageTonePhaseRadians = 0.0;
  float EngineAudioPitchMultiplier = 1.0f;
  float PropellerAudioPitchMultiplier = 1.0f;
  float CabinAudioPitchMultiplier = 1.0f;
  float AirflowAudioPitchMultiplier = 1.0f;
  float DamageAudioPitchMultiplier = 1.0f;

  UPROPERTY(VisibleAnywhere, Category="Flying|Cockpit")
  TArray<TObjectPtr<UStaticMeshComponent>> CockpitControlMeshes;

  UPROPERTY(VisibleAnywhere, Category="Flying|Cockpit")
  TArray<TObjectPtr<UTextRenderComponent>> CockpitControlLabels;

  UPROPERTY(VisibleAnywhere, Category="Flying|Cockpit")
  TArray<TObjectPtr<UTextRenderComponent>> InstrumentReadouts;

  UPROPERTY(VisibleAnywhere, Category="Flying|Cockpit")
  TArray<FFlyingCockpitControlState> CockpitControls;

  UPROPERTY(VisibleAnywhere, Category="Flying|Cockpit")
  bool bNightLightingEnabled = false;

  UPROPERTY(VisibleAnywhere, Category="Flying|Cockpit")
  EFlyingCockpitCameraMode ActiveCameraMode = EFlyingCockpitCameraMode::Pilot;

  UPROPERTY(VisibleAnywhere, Category="Flying|Components")
  TObjectPtr<UFlyingCoreSimComponent> CoreSimComponent;

  UPROPERTY(VisibleAnywhere, Category="Flying|Components")
  TObjectPtr<UFlyingCesiumGeoreferenceComponent> GeoreferenceComponent;

  FFlyingCoreSimImmutableStateSnapshot LastAppliedImmutableSnapshot;
  FVector LastAppliedUnrealLocation = FVector::ZeroVector;
  FRotator LastAppliedUnrealRotation = FRotator::ZeroRotator;
  bool bHasAppliedImmutableSnapshot = false;
};
