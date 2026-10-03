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

/** What the water and the air did to the board in its last fixed step, for debug drawing and telemetry. Forces in N, world frame. */
struct FBoardStepDebug
{
	/** Drag of the hull (planing or displacement), against the board's velocity over the water; on the plane it includes PressureDragN. */
	FVector DragForceN = FVector::ZeroVector;
	/** The water's normal force on the board (N): the weight it carries over the cosine of its heel, while the rider stands on it. */
	float NormalForceN = 0.0f;
	/** The planing pressure drag (N): the normal force tilted back by the trim, NormalForceN * tan(TrimDeg); only on the plane. */
	float PressureDragN = 0.0f;
	/** The planing trim the pressure drag used (deg), UBoardMovementComponent::GetPlaningTrimDeg at the board's speed; 0 off the plane. */
	float TrimDeg = 0.0f;
	/** Angle between the board's velocity over the water and its axis, either end first (deg); positive sliding to its right. */
	float LeewayDeg = 0.0f;
	/** Heel of the board (deg): positive heeled to hold a pull towards its right, as UBoardMovementComponent::GetHeelDeg. */
	float HeelDeg = 0.0f;
	/** Side force the fins and the immersed rail make from leeway, across the board's axis (normal to them). */
	FVector SideForceN = FVector::ZeroVector;
	/** Sideways part of the water's normal force on the heeled board, across its axis: N sin(heel). */
	FVector NormalSideForceN = FVector::ZeroVector;
	/** The horizontal line pull across the board's axis that the heel was set against (N, signed like HeelDeg). */
	float PullAcrossN = 0.0f;
	/** Weight the board carries: the rider's weight less the line's upward pull, never below zero (N). */
	float CarriedN = 0.0f;
	/** Air drag on the rider and board, along the wind they feel; only in the air. */
	FVector AirDragN = FVector::ZeroVector;
	/** Where the water was sampled under the board (world, cm, on the surface): centre, nose, tail, right rail, left rail. */
	FVector WaterSamplesCm[5] = { FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector };
	/** The plane fitted to the samples: its height under the board's centre (cm) and its normal. */
	float WaterHeightCm = 0.0f;
	FVector WaterNormal = FVector::UpVector;
	/** How fast the surface under the board is rising (cm/s): its height change per step along the board's path. */
	float SurfaceVerticalSpeedCmS = 0.0f;
	/** The water's vertical force on the board (N): the buoyancy spring and the planing lift, or the touchdown absorber while it is taking a sink out. */
	float WaterVerticalForceN = 0.0f;
	/** True while the touchdown absorber is taking the board's sink out. */
	bool bAbsorbing = false;
};

/** A clean landing, with its load in g: 1 + v^2 / (2 g s), v the sink into the water, s the absorb distance (UBoardMovementComponent::GetLastLandingG). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBoardLanding, float, LandingG);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBoardCrash, float, CrashIntensity);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBoardReset);
/** The board has left the water: popped by the rider (Jump) or lifted off by the kite (bPopped false). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBoardTakeoff, bool, bPopped);
/** The jump in progress has stopped rising at a new highest point: its height above the water (cm). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBoardApex, float, ApexHeightCm);

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

	/** Broadcast on every take-off, popped or lifted off by the kite. Tests and the trick tracker poll GetTakeoffCount instead. */
	UPROPERTY(BlueprintAssignable, Category = "Board|Events")
	FOnBoardTakeoff OnBoardTakeoff;

	/** Broadcast when the board stops rising at a new highest point of the jump (again after a kite yank lifts it higher). Polled as GetApexCount. */
	UPROPERTY(BlueprintAssignable, Category = "Board|Events")
	FOnBoardApex OnBoardApex;

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

	/**
	 * Holds the load: the rider crouches with their weight over the back of the board and drives
	 * the edge in against the lines. The board heels up to LoadExtraHeelDeg past the balance, so the
	 * water's normal force pushes it upwind of the pull and the lines pull harder; the board is held
	 * down as with a full edge, and the pop that follows is up to LoadPopBonus stronger. How much
	 * that raises the line tension depends on where the kite is: an edge resists a pull across
	 * the board, not one along it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Board|Jump")
	void SetLoadHeld(bool bHeld) { bLoadHeld = bHeld; }

	UFUNCTION(BlueprintPure, Category = "Board|Jump")
	bool IsLoadHeld() const { return bLoadHeld; }

	/** How far into the crouch the rider is, 0..1. */
	UFUNCTION(BlueprintPure, Category = "Board|Jump")
	float GetLoadAmount() const { return LoadAmount; }

	/** How fast the crouch builds while held, and lets go when released (1/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")
	float LoadRatePerSec;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")
	float LoadReleaseRatePerSec;

	/** Extra pop from a full load, as a fraction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")
	float LoadPopBonus;

	/**
	 * Pops the rider off the water. Always possible while they are up on the board on the water;
	 * refused (NotPlaning) in the air, during a crash, or while floating.
	 */
	UFUNCTION(BlueprintCallable, Category = "Board|Jump")
	EJumpRejectReason Jump();

	UFUNCTION(BlueprintPure, Category = "Board|Jump")
	static FString JumpRejectReasonToString(EJumpRejectReason Reason);

	UFUNCTION(BlueprintCallable, Category = "Board|Jump")
	bool WasLastLandingClean() const { return bLastLandingClean; }

	/**
	 * The last landing's load (g): 1 + v^2 / (2 g s), v the board's sink into the water as it touched
	 * down and s the distance the legs and the board's immersion took it out over,
	 * LandingAbsorbDistanceCm softened by the crouch (docs/physics/research.md 3.4). Set on every
	 * landing from a jump, clean or not; 1 before the first.
	 */
	UFUNCTION(BlueprintPure, Category = "Board|Jump")
	float GetLastLandingG() const { return LastLandingG; }

	/** The last landing's sink into the water (m/s), relative to the surface. */
	UFUNCTION(BlueprintPure, Category = "Board|Jump")
	float GetLastLandingSinkMS() const { return LastLandingSinkMS; }

	/** The distance the last landing's sink was taken out over (cm): LandingAbsorbDistanceCm times 1 + CrouchAbsorbBonus * the crouch. */
	UFUNCTION(BlueprintPure, Category = "Board|Jump")
	float GetLastLandingAbsorbCm() const { return LastLandingAbsorbCm; }

	/**
	 * True if the last landing was hot: the rider sank faster than HotLandingSinkMS or the kite was
	 * under HotLandingKiteElevationDeg as they touched down (docs/research.md C7). Not a crash by
	 * itself; a crash is CrashLandingG or MaxLandingAngle.
	 */
	UFUNCTION(BlueprintPure, Category = "Board|Jump")
	bool WasLastLandingHot() const { return bLastLandingHot; }

	/** The load of a landing at this sink into the water (m/s) taken out over this distance (cm), in g: 1 + v^2 / (2 g s). */
	UFUNCTION(BlueprintPure, Category = "Board|Jump")
	static float LandingGForSink(float SinkMS, float AbsorbDistanceCm);

	/**
	 * Angle between the board's axis and its horizontal velocity at the last landing from a jump
	 * (deg, 0 to 90: a twin-tip lands either way round). Over MaxLandingAngle it was a crash. Set
	 * with the other landing facts; 0 before the first.
	 */
	UFUNCTION(BlueprintPure, Category = "Board|Jump")
	float GetLastLandingAngleDeg() const { return LastLandingAngleDeg; }

	/**
	 * Take-offs so far, popped or lifted off by the kite, counted in BeginAirborne. A take-off that
	 * ends as a skip off the surface counts here but not in GetJumpCount. Polled by code that
	 * cannot rely on OnBoardTakeoff being bound (tests, the trick tracker).
	 */
	UFUNCTION(BlueprintPure, Category = "Board|Jump")
	int32 GetTakeoffCount() const { return TakeoffCount; }

	/** True if the last take-off was the rider's pop (Jump), false if the kite lifted them off. */
	UFUNCTION(BlueprintPure, Category = "Board|Jump")
	bool WasLastTakeoffPopped() const { return bLastTakeoffPopped; }

	/** Board simulation time of the last take-off (s, GetSimTimeSeconds): its landing comes GetLastJumpAirtime later. */
	UFUNCTION(BlueprintPure, Category = "Board|Jump")
	float GetLastTakeoffTimeSeconds() const { return LastTakeoffTimeSeconds; }

	/** How many times OnBoardApex has fired: once per jump, more if the kite lifts the rider higher after they started down. */
	UFUNCTION(BlueprintPure, Category = "Board|Jump")
	int32 GetApexCount() const { return ApexCount; }

	/** The highest the jump in progress (or the last one, until the next take-off) has been above the water (cm). */
	UFUNCTION(BlueprintPure, Category = "Board|Jump")
	float GetCurrentJumpApexHeight() const { return CurrentJumpApexHeight; }

	/**
	 * Board simulation time at which the jump in progress was highest (s); after a landing, the last
	 * jump's apex time until the next take-off. The take-off time before the board has risen.
	 */
	UFUNCTION(BlueprintPure, Category = "Board|Jump")
	float GetCurrentJumpApexTimeSeconds() const { return CurrentJumpApexTimeSeconds; }

	/** How many landings from a jump the board has made, clean or crashed (not the kite's skips): the same count as GetJumpCount. Polled by the HUD's landing card. */
	UFUNCTION(BlueprintPure, Category = "Board|Jump")
	int32 GetLandingCount() const { return JumpCount; }

	/** True while the touchdown absorber is taking a sink into the water out (the legs and the board's immersion). */
	UFUNCTION(BlueprintPure, Category = "Board|Physics")
	bool IsAbsorbingTouchdown() const { return bAbsorbing; }

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

	/** How far over the water the jump in progress has carried, from take-off (cm). */
	UFUNCTION(BlueprintCallable, Category = "Board|Jump")
	float GetCurrentJumpDistance() const { return CurrentJumpDistance; }

	/** Take-off to touchdown of the last jump, measured over the water (cm). */
	UFUNCTION(BlueprintCallable, Category = "Board|Jump")
	float GetLastJumpDistance() const { return LastJumpDistance; }

	UFUNCTION(BlueprintCallable, Category = "Board|Jump")
	float GetBestJumpDistance() const { return BestJumpDistance; }

	/** How many jumps have been landed or crashed: it goes up when a jump's figures are final. */
	UFUNCTION(BlueprintCallable, Category = "Board|Jump")
	int32 GetJumpCount() const { return JumpCount; }

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

	/**
	 * The water under the board as the last step saw it: a plane fitted to five samples (the centre, the
	 * nose and tail at WaterSampleAlongFraction of the length, and both rails at WaterSampleAcrossCm),
	 * its height under the board's centre (cm), its normal, and how fast it is rising under the board
	 * (cm/s). Pitch and roll on the water follow the plane.
	 */
	UFUNCTION(BlueprintPure, Category = "Board|Physics")
	float GetWaterSurfaceHeightCm() const { return LastStepDebug.WaterHeightCm; }

	UFUNCTION(BlueprintPure, Category = "Board|Physics")
	FVector GetWaterSurfaceNormal() const { return LastStepDebug.WaterNormal; }

	UFUNCTION(BlueprintPure, Category = "Board|Physics")
	float GetSurfaceVerticalSpeedCmS() const { return LastStepDebug.SurfaceVerticalSpeedCmS; }

	/** Sets the water surface interface (used by tests or custom water providers) */
	void SetWaterSurface(TSharedPtr<IKiteWaterSurface> InWaterSurface);

	/** Gets the active water surface interface */
	TSharedPtr<IKiteWaterSurface> GetWaterSurface() const;

	/** The forces on the board in the last fixed step: the water's drag, side force and normal force, the heel and leeway (zero in the air but the heel), and the air's drag (only in the air). */
	const FBoardStepDebug& GetLastStepDebug() const { return LastStepDebug; }

	/** Time the board's simulation has advanced (s); the wind on the rider in the air is sampled at this time. */
	UFUNCTION(BlueprintCallable, Category = "Board|Physics")
	float GetSimTimeSeconds() const { return SimTimeSeconds; }

public:
	// Tunables (Spec)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float MassKg;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float BoardLengthCm;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float BoardWidthCm;

	/** The nose and tail water samples are this fraction of BoardLengthCm ahead of and behind the board's centre (docs/physics/plan-2.md item 4). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Water", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float WaterSampleAlongFraction;

	/** The rail water samples are this far either side of the board's centre line (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Water", meta = (ClampMin = "0.0"))
	float WaterSampleAcrossCm;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float BuoyancyN;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float PlaningThresholdCmS;

	/** Displacement drag per speed squared (kg/cm): force in kg*cm/s^2 is this times (cm/s)^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float DisplacementQuadraticDragKgPerCm;

	/** Planing drag per speed (kg/s): with PlaningQuadraticDragKgPerCm, the drag that grows with speed, on top of the pressure drag of PlaningTrimDeg. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float PlaningDragKgPerS;

	/** Planing drag per speed squared (kg/cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float PlaningQuadraticDragKgPerCm;

	/**
	 * Trim of the planing board at speed (deg), from PlaningTrimHumpSpeedCmS up. The water's normal
	 * force on the planing surface leans back by the trim, so it drags the board by N tan(trim), N the
	 * weight it carries over the cosine of its heel (Savitsky's pressure drag, docs/physics/research.md
	 * 2.2: trim 6 to 10 deg). Carrying the edge therefore costs drag: the more the board heels, the
	 * larger N. See GetPlaningTrimDeg.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float PlaningTrimDeg;

	/**
	 * Trim of the board just over the planing threshold (deg). A planing hull trims highest just past
	 * the hump and flattens out as it speeds up (Savitsky), so a board slowed towards the threshold,
	 * pointed high or loaded, pays more pressure drag for the weight it carries. The trim falls from
	 * this at PlaningThresholdCmS to PlaningTrimDeg at PlaningTrimHumpSpeedCmS, linearly in speed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float PlaningTrimHumpDeg;

	/** Speed by which the planing trim has fallen from PlaningTrimHumpDeg to PlaningTrimDeg (cm/s); scaled with the planing threshold by SetBoardSize. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning", meta = (ClampMin = "0.0"))
	float PlaningTrimHumpSpeedCmS;

	/** The planing board's trim at this speed over the water (deg): PlaningTrimHumpDeg at the planing threshold, falling to PlaningTrimDeg by PlaningTrimHumpSpeedCmS. */
	UFUNCTION(BlueprintPure, Category = "Board|Physics")
	float GetPlaningTrimDeg(float SpeedCmS) const;

	/** How far the full carve input turns the board's heading off its course (deg); also the default CarveHeelDeg. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float MaxEdgeAngleDeg;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float MaxBoardSpeedCmS;

	/** Displacement drag per speed (kg/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float DisplacementDragKgPerS;

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

	/**
	 * Drag area of a rider floating in the water with the board sunk (m^2): a body sitting in the
	 * water and a board under it are slow to pull through it in any direction. While floating the
	 * water drags them with 0.5 rho_w A v^2 against the horizontal velocity, A this times how far past
	 * half the floating depth they are (none at IsFloating's threshold, all of it fully sunk), fading
	 * out by FloatingDragFadeSpeedCmS. Nothing on the plane (docs/physics/plan-2.md item 3d).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning", meta = (ClampMin = "0.0"))
	float FloatingDragAreaM2;

	/**
	 * Speed by which a floating rider pulled through the water has come up to plane on their back and
	 * the board, and FloatingDragAreaM2 no longer acts (cm/s); it fades out from rest (smoothstep). The
	 * float depth only starts to rise at FloatUntilSpeedFraction of planing speed, and a drag held at
	 * full depth up to there would take more pull than a parked kite gives to get through: a
	 * transition, a slow start or a water start would leave the rider stuck. 0 = no fade.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning", meta = (ClampMin = "0.0"))
	float FloatingDragFadeSpeedCmS;

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

	/** Extra planing drag with the weight fully on the tail, as a fraction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float TailWeightDrag;

	/** Planing drag saved with the weight fully on the nose, as a fraction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float NoseWeightDragSaving;

	/** Board pitch at full weight shift on the water (deg): nose down forward, nose up back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float WeightShiftPitchDeg;

	/** Board pitch at full weight shift in the air (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")
	float AirWeightShiftPitchDeg;

	/**
	 * How much more upward pull than their weight a loaded rider hangs on to, in body weights at full
	 * load: the board leaves the water when the lines pull up harder than m g (1 + LoadHoldBonus *
	 * load). Unloaded, any pull above the rider's weight lifts them. Research: take-off at 2.5 to 4
	 * body weights of tension (docs/physics/research.md 3.3), so 1.5 to 3.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump", meta = (ClampMin = "0.0"))
	float LoadHoldBonus;

	/** Board spin rate in the air at full carve input (deg/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")
	float AirSpinRate;

	/** Extra pop with the weight fully on the tail, as a fraction of PopImpulseKgCmPerS. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")
	float TailWeightPopBonus;

	/** Tail-first speed at which the board swaps nose and tail (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float SwitchStanceSpeedCmS;

	/**
	 * The rider heels the board to the balance angle without being asked, as a rider's body does:
	 * tan(heel) = sideways pull / (weight - upward pull) (docs/physics/research.md 3.1). Off, the
	 * board rides flat unless the load heels it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Edging")
	bool bAutoEdge;

	/** Most the board heels over (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Edging", meta = (ClampMin = "0.0", ClampMax = "85.0"))
	float MaxHeelDeg;

	/** Heel added on top of the balance at full load (deg): loading is edging harder than the pull needs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Edging", meta = (ClampMin = "0.0"))
	float LoadExtraHeelDeg;

	/** How quickly the heel follows its target (1/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Edging", meta = (ClampMin = "0.0"))
	float HeelResponse;

	/**
	 * Heel the full carve input adds towards the inside of the turn (deg), on top of the balance: the
	 * rider leans into the carve, so the water's normal force on the board tilts into the turn and
	 * pulls the velocity round after the heading (docs/physics/plan-2.md item 3c). Turning towards the
	 * kite it takes the edge off and lets the pull across turn the board; turning away it digs the
	 * rail in harder. The board is drawn at this heel too.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Edging", meta = (ClampMin = "0.0"))
	float CarveHeelDeg;

	/**
	 * Lateral area of the fins (m^2). Four fins 4 to 5 cm deep (research 2.3) on a 10 to 12 cm base
	 * are 0.003 to 0.005 m^2 each. They are all a flat board has, and they cannot hold a riding pull
	 * on their own: a board ridden flat slides downwind.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Edging", meta = (ClampMin = "0.0"))
	float FinAreaM2;

	/** Lateral area of the immersed rail at full heel (m^2); at heel phi it is this times sin(phi), times the weight shift's TailWeightRailScale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Edging", meta = (ClampMin = "0.0"))
	float RailAreaM2;

	/** Side force coefficient per radian of leeway of the fins and rail (1/rad): 2 to 3 for these low aspect ratios (research 2.3). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Edging", meta = (ClampMin = "0.0"))
	float LateralLiftSlopePerRad;

	/** Leeway past which the side force grows no more (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Edging", meta = (ClampMin = "0.1"))
	float LeewayStallDeg;

	/** Rail area multiplier with the weight fully on the tail, which sinks the tail and digs the rail in; fully on the nose gets the inverse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Edging", meta = (ClampMin = "0.01"))
	float TailWeightRailScale;

	/** The board's heel (deg): positive when heeled to hold a pull towards the board's right, negative towards its left; it includes the lean into a carve (CarveHeelDeg). */
	UFUNCTION(BlueprintPure, Category = "Board|Physics")
	float GetHeelDeg() const { return HeelDeg; }

	/** Angle between the board's velocity over the water and its axis, either end first (deg), as last stepped; positive sliding to its right. */
	UFUNCTION(BlueprintPure, Category = "Board|Physics")
	float GetLeewayDeg() const { return LastStepDebug.LeewayDeg; }

	/**
	 * Drag area (drag coefficient times frontal area, m^2) of the rider and board in the air. While
	 * airborne the air pushes on them with 0.5 * rho * CdA * |v_a| * v_a, v_a the wind they feel (the
	 * true wind at the kite's RiderWindHeightCm above them, minus their velocity); nothing on the
	 * water. Research: 0.5 to 1.0 m^2 (docs/physics/research.md 3.5).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump", meta = (ClampMin = "0.0"))
	float RiderDragAreaM2;

	/** Vertical impulse from the legs on a pop (kg*cm/s): about 2.5 m/s for 85 kg. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")
	float PopImpulseKgCmPerS;

	/**
	 * How long the kite's upward pull counts as an extra impulse when the edge is let go (s). 0 =
	 * physics only: the pop is the legs alone, and the height comes from the lines' pull acting on
	 * the rider as a force once they are off the water (docs/physics/plan-2.md item 2, A3). Phase 1
	 * used 0.22, a pseudo-impulse that gave the timed jump at 30 kn 5.7 of its 10 m/s of take-off
	 * speed; set it back to have that feel.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump", meta = (ClampMin = "0.0"))
	float EdgeReleaseSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")
	float JumpMinSpeedKnots;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")
	float JumpMinEdgeInput;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")
	float MaxJumpHeight;

	/** Most angle between the board's axis and its course over the water at touchdown for a clean landing (deg); past it the rider crashes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")
	float MaxLandingAngle;

	/**
	 * Distance over which a touchdown's sink into the water is taken out (cm): the legs bending and the
	 * board's immersion. The water then decelerates the sink v at a constant v^2 / (2 s), and the
	 * landing's load is 1 + v^2 / (2 g s) (docs/physics/research.md 3.4: 0.2 to 0.4 m; 4 m/s into 0.3 m
	 * is 3.7 g, 7 m/s is 9 g).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Landing", meta = (ClampMin = "1.0"))
	float LandingAbsorbDistanceCm;

	/** How much longer a full crouch (GetLoadAmount 1, the jump button held) makes the absorb distance, as a fraction: 1 doubles it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Landing", meta = (ClampMin = "0.0"))
	float CrouchAbsorbBonus;

	/** A landing harder than this (g) is a crash, however well the board is lined up. Research: measured landings 4.2 to 5.5 g. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Landing", meta = (ClampMin = "1.0"))
	float CrashLandingG;

	/** A landing sinking faster than this into the water (m/s) is hot (research: 3 to 6 m/s with the kite overhead, 8 to 12 with it low). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Landing", meta = (ClampMin = "0.0"))
	float HotLandingSinkMS;

	/** A landing with the kite under this elevation above the rider (deg) is hot: the kite is not holding them up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Landing", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float HotLandingKiteElevationDeg;

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
	float LoadAmount = 0.0f;
	bool bLoadHeld = false;
	float SmoothedCarveInput;
	bool bLiftedByKite;
	/** The board's heel (deg), signed as GetHeelDeg: BalanceHeelDeg less the lean into a carve. */
	float HeelDeg = 0.0f;
	/** The heel the rider holds against the pull (deg), the balance plus the load's extra, followed at HeelResponse. */
	float BalanceHeelDeg = 0.0f;

	/**
	 * Moves the heel towards its target for this step and returns the sideways part of the water's
	 * normal force on the heeled board (kg*cm/s^2), across its axis; the whole normal force (N) goes
	 * in LastStepDebug.NormalForceN. PullN is the external force on the board this step (N); bOnWater
	 * says the rider is standing on the board on the water.
	 */
	FVector UpdateHeelAndNormalSideForce(float DeltaTime, const FVector& PullN, const FVector& LevelRight, bool bOnWater);

	/**
	 * The hull's drag against the horizontal velocity and the fins' and rail's side force from leeway,
	 * both integrated without overshoot over the step: the drag shortens the velocity (first the
	 * constant pressure drag, DragDecelCmS2, then a + c v^2 exactly), and the side force, across the
	 * board's axis, takes out the speed across it and so turns the velocity onto the axis.
	 */
	void ApplyWaterDragAndSideForce(float DeltaTime, float DragDecelCmS2, float DragRatePerS, float DragRatePerCm, const FVector& LevelForward);

	/**
	 * Puts the board in the air and starts the jump telemetry: counts the take-off, notes whether the
	 * rider popped (Jump) or the kite lifted them off, and broadcasts OnBoardTakeoff.
	 */
	void BeginAirborne(bool bPopped);

	/**
	 * Notes the facts of a landing from a jump that the board's own landing code does not keep:
	 * the landing angle (deg). Called from the landing block once the jump is counted, clean or not.
	 */
	void NoteJumpEnd(float LandingAngleDeg);

	/** Take-off and apex telemetry (GetTakeoffCount and the rest). */
	int32 TakeoffCount = 0;
	bool bLastTakeoffPopped = false;
	float LastTakeoffTimeSeconds = 0.0f;
	int32 ApexCount = 0;
	float CurrentJumpApexTimeSeconds = 0.0f;
	/** The board has risen since take-off or the last apex; the next step that does not rise is an apex. */
	bool bJumpRising = false;
	/** Height of the last apex broadcast in this jump (cm); an apex fires again only above it. Negative before the first. */
	float LastApexEventHeightCm = -1.0f;
	float LastLandingAngleDeg = 0.0f;

	/**
	 * Samples the water at the five points under a board at Location heading Yaw, fits a plane to them
	 * and fills the water fields of LastStepDebug: the plane's height under the centre, its normal and
	 * the surface's vertical speed under the board since the last step (DeltaTime s ago).
	 */
	void SampleWaterUnderBoard(const FVector& Location, float Yaw, float DeltaTime);

	/** Where and how high the water under the board's centre was at the last step, for the surface's vertical speed. */
	bool bHasWaterTrack = false;
	FVector LastWaterTrackLocation = FVector::ZeroVector;
	float LastWaterTrackHeightCm = 0.0f;

	/**
	 * Starts the touchdown absorber for a sink into the water of SinkCmS (relative to the surface): a
	 * constant deceleration of SinkCmS^2 / (2 s) over s = LandingAbsorbDistanceCm softened by the
	 * crouch, until the board moves with the surface. Returns s (cm).
	 */
	float BeginTouchdownAbsorb(float SinkCmS);

	/** The touchdown absorber: on, its deceleration of the sink (cm/s^2), and whether the board was in contact with the water at the last step. */
	bool bAbsorbing = false;
	float AbsorbDecelCmS2 = 0.0f;
	bool bWasInWaterContact = true;

	float LastLandingG = 1.0f;
	float LastLandingSinkMS = 0.0f;
	float LastLandingAbsorbCm = 0.0f;
	bool bLastLandingHot = false;

	/** Speed after Seconds of linear plus quadratic drag, integrated exactly. */
	static float DecayWithLinearAndQuadraticDrag(float Speed, float LinearRatePerS, float QuadraticRatePerCm, float Seconds);
	float EffectiveMassForBuoyancy() const;
	FVector AccumulatedExternalForce;
	FBoardStepDebug LastStepDebug;

	/** Time the board's simulation has advanced (s). */
	float SimTimeSeconds = 0.0f;

	/** Air drag on the rider and board at this place and velocity (kg*cm/s^2), from the wind at the board's simulation time. */
	FVector ComputeAirDragForce(const FVector& Location, const FVector& InVelocity) const;

	float CurrentJumpHeight;
	float CurrentJumpAirtime;
	float CurrentJumpApexHeight;
	float LastJumpApexHeight;
	float LastJumpAirtime;
	float BestJumpHeight;
	FVector JumpStartLocation = FVector::ZeroVector;
	float CurrentJumpDistance = 0.0f;
	float LastJumpDistance = 0.0f;
	float BestJumpDistance = 0.0f;
	int32 JumpCount = 0;

	bool bLastLandingClean;
	bool bIsCrashing;
	float CrashTimer;
	FVector CrashInitialVelocity;
	float LandingStateTimer;

	mutable TWeakObjectPtr<const UWaterBodyComponent> CachedWaterBodyComponent;
	mutable TSharedPtr<IKiteWaterSurface> WaterSurface;
};
