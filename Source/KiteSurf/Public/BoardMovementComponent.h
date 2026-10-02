#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "KiteGear.h"
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

	/** Advances the board by DeltaTime in sub-steps no longer than MaxStepSeconds; a force added before the call acts for the whole of it. For tests and for a board nobody else steps. */
	UFUNCTION(BlueprintCallable, Category = "Board|Physics")
	void Simulate(float DeltaTime);

	/** One fixed step with the external force accumulated since the last one. The pawn calls this inside its own step loop. */
	UFUNCTION(BlueprintCallable, Category = "Board|Physics")
	void StepBoard(float StepSeconds);

	/** Accumulate an external force in kg*cm/s^2, applied during the next step */
	UFUNCTION(BlueprintCallable, Category = "Board|Physics")
	void AddExternalForce(const FVector& Force);

	/** Set heel/toe edge input clamped to [-1.0, 1.0] */
	UFUNCTION(BlueprintCallable, Category = "Board|Input")
	void SetEdgeInput(float Value);

	UFUNCTION(BlueprintCallable, Category = "Board|Input")
	float GetEdgeInput() const { return CurrentEdgeInput; }

	/**
	 * Where the rider's weight is along the board: +1 on the nose, -1 on the tail. Clamped to [-1, 1].
	 * Weight back sinks the tail: more grip, more drag, a loaded edge and a bigger pop.
	 * Weight forward flattens the board: less drag and less grip, so it runs faster and slides more.
	 * In the air it tips the board nose-down or nose-up.
	 */
	UFUNCTION(BlueprintCallable, Category = "Board|Input")
	void SetWeightShift(float Value);

	UFUNCTION(BlueprintCallable, Category = "Board|Input")
	float GetWeightShift() const { return CurrentWeightShift; }

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

	/** Puts the rider on a board of this size: pop, the speed it planes at, drag, grip and turning follow it. */
	UFUNCTION(BlueprintCallable, Category = "Board")
	void SetBoardSize(EBoardSize InSize);

	UFUNCTION(BlueprintPure, Category = "Board")
	EBoardSize GetBoardSize() const { return BoardSize; }

	/** How many times the rider has been put back on the board by ResetToTack. Polled by code that cannot rely on OnBoardReset being bound. */
	UFUNCTION(BlueprintCallable, Category = "Board|Physics")
	int32 GetResetCount() const { return ResetCount; }

	/** How far below the water surface the board is riding now (cm): 0 when planing, FloatSubmersionCm when floating. */
	UFUNCTION(BlueprintCallable, Category = "Board|Physics")
	float GetFloatDepthCm() const { return CurrentFloatDepthCm; }

	/** True while the rider is in the water rather than up on the board: more than half sunk. */
	UFUNCTION(BlueprintCallable, Category = "Board|Physics")
	bool IsFloating() const { return CurrentFloatDepthCm > 0.5f * FloatSubmersionCm; }

	/** The depth the board settles to at a given speed (cm). */
	UFUNCTION(BlueprintCallable, Category = "Board|Physics")
	float GetFloatDepthForSpeed(float SpeedCmS) const;

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

	/** Displacement drag per speed squared (kg/cm): force in kg*cm/s^2 is this times (cm/s)^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float DisplacementQuadraticDragKgPerCm;

	/** Planing drag per speed (kg/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float PlaningDragKgPerS;

	/** Planing drag per speed squared (kg/cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float PlaningQuadraticDragKgPerCm;

	/** Extra sideways grip at full carve input (kg/s): sideways force per cm/s of leeway. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float EdgeGripKgPerS;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float MaxEdgeAngleDeg;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float MaxBoardSpeedCmS;

	/** Displacement drag per speed (kg/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float DisplacementDragKgPerS;

	/** Share of the sideways grip force that a heeled rail turns into forward drive, at full carve input. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float EdgeDriveEfficiency;

	/** Sideways grip with no carve input (kg/s): the fins and a neutral stance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float BaseGripKgPerS;

	/** How quickly the board bobs back to its ride height (Hz). Stiffness scales with the mass, so the feel does not change with the rider. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning", meta = (ClampMin = "0.01"))
	float BuoyancyNaturalFrequencyHz;

	/** Damping of that bob: 1 settles without overshoot, below 1 bounces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning", meta = (ClampMin = "0.0"))
	float BuoyancyDampingRatio;

	/** Upward planing lift per cm/s above the planing speed (kg/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float PlaningLiftKgPerS;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float CarveTurnRate;

	/** How deep the board sits below the surface with the rider floating (cm): about chest deep. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float FloatSubmersionCm;

	/** The rider floats fully sunk below this fraction of planing speed and is on the surface at planing speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning", meta = (ClampMin = "0.0", ClampMax = "0.99"))
	float FloatUntilSpeedFraction;

	/** How quickly the rider sinks or rises as the speed changes (1/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float FloatResponse;

	/** Below this speed the board pivots to point along the kite's pull (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float LowSpeedPivotMaxSpeedCmS;

	/** Pivot rate when stopped (deg/s); eases to a fifth of this at LowSpeedPivotMaxSpeedCmS. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float LowSpeedPivotRate;

	/** Horizontal line force needed before the board pivots towards it (N). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float LowSpeedPivotMinForceN;

	/** How quickly the carve follows the input (1/s); lower feels heavier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float CarveResponse;

	/** Grip multiplier with the weight fully on the tail; fully on the nose gets the inverse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float TailWeightGripScale;

	/** Extra planing drag with the weight fully on the tail, as a fraction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float TailWeightDrag;

	/** Planing drag saved with the weight fully on the nose, as a fraction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float NoseWeightDragSaving;

	/** Extra heel with the weight fully on the tail (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float TailWeightHeelDeg;

	/** Board pitch at full weight shift on the water (deg): nose down forward, nose up back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float WeightShiftPitchDeg;

	/** Board pitch at full weight shift in the air (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")
	float AirWeightShiftPitchDeg;

	/** The kite lifts the rider off the water when its upward pull exceeds this multiple of their weight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")
	float LiftoffWeightFactor;

	/** Added to LiftoffWeightFactor at full edge (turn input or weight on the tail): holding the edge holds the rider down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning", meta = (ClampMin = "0.0"))
	float EdgedLiftoffWeightBonus;

	/** Board spin rate in the air at full carve input (deg/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")
	float AirSpinRate;

	/** Extra pop with the weight fully on the tail, as a fraction of PopImpulseKgCmPerS. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")
	float TailWeightPopBonus;

	/** Tail-first speed at which the board swaps nose and tail (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float SwitchStanceSpeedCmS;

	/** Heel angle away from the kite at full sideways load (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float AutoHeelDeg;

	/** Sideways line force that gives the full AutoHeelDeg (N). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float AutoHeelFullLoadN;

	/** Vertical impulse from the legs on a pop (kg*cm/s): about 2.5 m/s for 85 kg. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")
	float PopImpulseKgCmPerS;

	/** How long the kite's upward pull counts as an impulse when the edge is let go (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")
	float EdgeReleaseSeconds;

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
	float GetMaxBoardSpeedCmS() const { return MaxBoardSpeedCmS; }

	/** The board is stepped in sub-steps no longer than this (s), whatever Simulate is given. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Simulation", meta = (ClampMin = "0.0001"))
	float MaxStepSeconds;

	/** Most sub-steps one Simulate call will take; past that the sub-step grows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Simulation", meta = (ClampMin = "1"))
	int32 MaxStepsPerUpdate;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	EBoardDragRegime CurrentDragRegime;

	UPROPERTY(Transient)
	EBoardState CurrentBoardState;

	float CurrentEdgeInput;
	float CurrentWeightShift;
	float CurrentFloatDepthCm;
	int32 ResetCount = 0;
	EBoardSize BoardSize = EBoardSize::Medium;
	float SmoothedCarveInput;
	bool bLiftedByKite;

	/** Puts the board in the air and starts the jump telemetry. */
	void BeginAirborne();

	/** Speed after Seconds of linear plus quadratic drag, integrated exactly. */
	static float DecayWithLinearAndQuadraticDrag(float Speed, float LinearRatePerS, float QuadraticRatePerCm, float Seconds);
	float EffectiveMassForBuoyancy() const;
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
