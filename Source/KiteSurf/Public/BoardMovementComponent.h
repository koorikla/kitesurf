#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "BoardMovementComponent.generated.h"

class AWaterBody;
class UWaterBodyComponent;
class IKiteWaterSurface;

UENUM(BlueprintType)
enum class EBoardDragRegime : uint8
{
	Displacement UMETA(DisplayName = "Displacement"),
	Planing      UMETA(DisplayName = "Planing")
};

UENUM(BlueprintType)
enum class EBoardState : uint8
{
	Displacement UMETA(DisplayName = "Displacement"),
	Planing      UMETA(DisplayName = "Planing"),
	Airborne     UMETA(DisplayName = "Airborne"),
	Landing      UMETA(DisplayName = "Landing")
};

UENUM(BlueprintType)
enum class EJumpRejectReason : uint8
{
	None       UMETA(DisplayName = "None"),
	NotPlaning UMETA(DisplayName = "Not Planing"),
	TooSlow    UMETA(DisplayName = "Too Slow"),
	NotEdged   UMETA(DisplayName = "Not Edged")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBoardLanding, float, LandingG);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBoardCrash, float, CrashIntensity);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBoardReset);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class KITESURF_API UBoardMovementComponent : public UPawnMovementComponent
{
	GENERATED_BODY()

public:
	UBoardMovementComponent();

	UPROPERTY(BlueprintAssignable, Category = "Board|Events")
	FOnBoardLanding OnBoardLanding;

	UPROPERTY(BlueprintAssignable, Category = "Board|Events")
	FOnBoardCrash OnBoardCrash;

	UPROPERTY(BlueprintAssignable, Category = "Board|Events")
	FOnBoardReset OnBoardReset;

	UFUNCTION(BlueprintCallable, Category = "Board|State")
	void TriggerCrash(float CrashIntensity = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Board|State")
	void ResetToTack(float SpeedKnots = 8.0f);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Accumulate an external force in kg*cm/s^2 (applied during next tick) */
	UFUNCTION(BlueprintCallable, Category = "Board|Physics")
	void AddExternalForce(const FVector& Force);

	/** Set heel/toe edge input clamped to [-1.0, 1.0] */
	UFUNCTION(BlueprintCallable, Category = "Board|Input")
	void SetEdgeInput(float Value);

	UFUNCTION(BlueprintCallable, Category = "Board|Input")
	float GetEdgeInput() const { return CurrentEdgeInput; }

	UFUNCTION(BlueprintCallable, Category = "Board|Physics")
	bool IsPlaning() const { return CurrentDragRegime == EBoardDragRegime::Planing; }

	UFUNCTION(BlueprintCallable, Category = "Board|Physics")
	EBoardDragRegime GetCurrentDragRegime() const { return CurrentDragRegime; }

	UFUNCTION(BlueprintCallable, Category = "Board|State")
	EBoardState GetBoardState() const { return CurrentBoardState; }

	UFUNCTION(BlueprintCallable, Category = "Board|State")
	void SetBoardState(EBoardState InState) { CurrentBoardState = InState; }

	/** Attempt to trigger a kite-powered jump. Returns EJumpRejectReason::None if jump conditions were met, or specific rejection reason. */
	UFUNCTION(BlueprintCallable, Category = "Board|Jump")
	EJumpRejectReason Jump();

	UFUNCTION(BlueprintPure, Category = "Board|Jump")
	static FString JumpRejectReasonToString(EJumpRejectReason Reason);

	UFUNCTION(BlueprintCallable, Category = "Board|Jump")
	bool WasLastLandingClean() const { return bLastLandingClean; }

	UFUNCTION(BlueprintCallable, Category = "Board|Jump")
	bool IsCrashing() const { return bIsCrashing; }

	UFUNCTION(BlueprintCallable, Category = "Board|Jump")
	float GetCurrentJumpHeight() const { return CurrentJumpHeight; }

	UFUNCTION(BlueprintCallable, Category = "Board|Jump")
	float GetCurrentJumpAirtime() const { return CurrentJumpAirtime; }

	UFUNCTION(BlueprintCallable, Category = "Board|Jump")
	void SetCurrentJumpAirtime(float Value) { CurrentJumpAirtime = Value; }

	UFUNCTION(BlueprintCallable, Category = "Board|Jump")
	float GetLastJumpApexHeight() const { return LastJumpApexHeight; }

	UFUNCTION(BlueprintCallable, Category = "Board|Jump")
	float GetLastJumpAirtime() const { return LastJumpAirtime; }

	UFUNCTION(BlueprintCallable, Category = "Board|Jump")
	float GetBestJumpHeight() const { return BestJumpHeight; }

	UFUNCTION(BlueprintCallable, Category = "Board|Physics")
	float GetForwardSpeed() const;

	UFUNCTION(BlueprintCallable, Category = "Board|Physics")
	float GetLateralSpeed() const;

	/** Samples water height and normal at the given world location */
	void SampleWaterSurface(const FVector& Location, float& OutWaterHeight, FVector& OutWaterNormal) const;

	/** Sets the water surface interface (used by tests or custom water providers) */
	void SetWaterSurface(TSharedPtr<IKiteWaterSurface> InWaterSurface);

	/** Gets the active water surface interface */
	TSharedPtr<IKiteWaterSurface> GetWaterSurface() const;

public:
	// Tunables (Spec)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float MassKg;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float BoardLengthCm;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float BoardWidthCm;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float BuoyancyN;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float PlaningThresholdCmS;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float DisplacementDragCoef;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float PlaningDragCoef;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float EdgeGripCoef;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float MaxEdgeAngleDeg;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float MaxBoardSpeed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float LinearDisplacementDragCoef;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float EdgeDriveEfficiency;

	// Additional physics tuning
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float BaseLateralDragCoef;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float BuoyancySpringStiffness;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float BuoyancyDamping;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float PlaningLiftCoef;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float CarveTurnRate;

	// Jump tunables (Spec)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")
	float BaseJumpImpulse;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")
	float KiteLiftFactor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")
	float JumpMinSpeedKnots;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")
	float JumpMinEdgeInput;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")
	float MaxJumpHeight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")
	float MaxLandingAngle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")
	float CleanLandingSpeedRetention;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")
	float CrashDecelDuration;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")
	float CrashRespawnDelay;

	UFUNCTION(BlueprintPure, Category = "Tuning")
	float GetMaxBoardSpeedCmS() const { return MaxBoardSpeed <= 100.0f ? (MaxBoardSpeed * 51.44f) : MaxBoardSpeed; }

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	EBoardDragRegime CurrentDragRegime;

	UPROPERTY(Transient)
	EBoardState CurrentBoardState;

	float CurrentEdgeInput;
	FVector AccumulatedExternalForce;

	float CurrentJumpHeight;
	float CurrentJumpAirtime;
	float CurrentJumpApexHeight;
	float LastJumpApexHeight;
	float LastJumpAirtime;
	float BestJumpHeight;

	bool bLastLandingClean;
	bool bIsCrashing;
	float CrashTimer;
	FVector CrashInitialVelocity;
	float LandingStateTimer;

	mutable TWeakObjectPtr<const UWaterBodyComponent> CachedWaterBodyComponent;
	mutable TSharedPtr<IKiteWaterSurface> WaterSurface;
};
