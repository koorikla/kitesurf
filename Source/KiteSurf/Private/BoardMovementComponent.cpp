#include "BoardMovementComponent.h"
#include "KiteSurf.h"
#include "KiteComponent.h"
#include "KiteWaterSurface.h"
#include "WindComponent.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"
#include "KiteSurfUnits.h"
#include "Tricks/LandingMath.h"

namespace
{
	// The reference (138 cm) board's planing drag that grows with speed, a + c v^2. Re-based when the
	// pressure drag of the trim came in (docs/physics/plan-2.md item 3b) so that the flat board
	// carrying the default 85 kg at 10 m/s still drags 380 N: 87.6 N of pressure drag at 6 deg of
	// trim and 292.5 N from these (they were 8 kg/s and 0.03 kg/cm, 380 N on their own).
	constexpr float ReferencePlaningDragKgPerS = 6.15f;
	constexpr float ReferencePlaningQuadraticDragKgPerCm = 0.0231f;

	// The surface's vertical speed under the board is its height change per step along the board's
	// path. A board moved further than its velocity explains (put somewhere by a reset or a test) has no
	// path to measure along; then the speed comes from the fitted slope alone.
	constexpr float WaterTrackJumpToleranceCm = 1.0f;

	// The board is in contact with the water within this height of the surface (cm): below it a
	// touchdown starts, and above it a board in the air has no water forces. The planing lift fades in
	// from the same height.
	constexpr float WaterContactHeightCm = 10.0f;

	// The buoyancy spring reaches this far above the ride height (cm).
	constexpr float BuoyancyReachCm = 15.0f;

	// The planing lift fades in from WaterContactHeightCm above the ride height to this far below it (cm).
	constexpr float PlaningLiftFullDepthCm = 5.0f;

	// The touchdown absorber is done once the board sinks into the water slower than this (cm/s).
	constexpr float AbsorbDoneSinkCmS = 1.0f;

	// A loaded rider's legs keep the board within this height above its ride height until the lines
	// lift them off (cm): the band the 20 cm water-contact clamp kept every board in before plan-2 item 4.
	constexpr float LoadHoldHeightCm = 20.0f;

	// Below this horizontal line pull (N) its direction across the water means little (the kite overhead,
	// or the lines barely loaded), and the harness does not limit the heading (docs/physics/plan-3.md
	// item 3).
	constexpr float HarnessMinHorizontalPullN = 50.0f;
}

UBoardMovementComponent::UBoardMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Tunables (Spec defaults)
	MassKg = 85.0f;
	BoardLengthCm = 140.0f;
	BoardWidthCm = 42.0f;
	WaterSampleAlongFraction = 0.45f; // nose and tail samples 63 cm either side of the centre
	WaterSampleAcrossCm = 18.0f;      // the rails
	BuoyancyN = 1500.0f;
	PlaningThresholdCmS = 400.0f;
	DisplacementQuadraticDragKgPerCm = 0.1f;
	PlaningDragKgPerS = ReferencePlaningDragKgPerS;
	PlaningQuadraticDragKgPerCm = ReferencePlaningQuadraticDragKgPerCm; // mostly quadratic, so board speed scales with wind speed
	PlaningTrimDeg = 6.0f;              // research 6 to 10 deg (Savitsky)
	PlaningTrimHumpDeg = 10.0f;         // just over the hump the hull trims highest: the top of research's 6 to 10
	PlaningTrimHumpSpeedCmS = 600.0f;   // 1.5 times the planing threshold
	MaxEdgeAngleDeg = 35.0f;
	MaxBoardSpeedCmS = KiteUnits::KnotsToCmS(35.0f);
	DisplacementDragKgPerS = 8.0f;

	// Additional physics tuning
	BuoyancyNaturalFrequencyHz = 0.945f; // the same as a 3000 kg/s^2 spring on 85 kg
	BuoyancyDampingRatio = 0.79f;
	PlaningLiftKgPerS = 50.0f;
	CarveTurnRate = 60.0f; // deg/s at full turn input once planing: about a 10 m carve at 20 kn
	SwitchStanceSpeedCmS = 100.0f;
	CarveResponse = 6.0f;
	LowSpeedPivotMaxSpeedCmS = 400.0f; // the planing threshold: a planing board holds its own course
	LowSpeedPivotRate = 120.0f;
	LowSpeedPivotMinForceN = 80.0f;
	FloatSubmersionCm = 85.0f;
	FloatUntilSpeedFraction = 0.45f;
	FloatResponse = 2.5f;
	FloatingDragAreaM2 = 0.35f;        // a rider sitting in the water and a sunk board: research 0.3 to 0.5
	FloatRiseTensionN = 0.6f * MassKg * KiteUnits::GravityMS2; // 500 N: about 0.6 body weights lifts the rider onto the board
	CurrentFloatDepthCm = 0.0f;
	TailWeightDrag = 0.35f;
	NoseWeightDragSaving = 0.15f;
	WeightShiftPitchDeg = 8.0f;
	AirWeightShiftPitchDeg = 30.0f;
	LoadHoldBonus = 1.5f; // a crouched, loaded rider hangs on to 2.5 body weights of upward pull
	LoadRatePerSec = 2.5f;        // a full crouch in 0.4 s
	LoadReleaseRatePerSec = 6.0f;
	LoadPopBonus = 0.6f;
	LoadAmount = 0.0f;
	bLoadHeld = false;
	AirSpinRate = 200.0f;
	TailWeightPopBonus = 0.5f;
	RiderDragAreaM2 = 0.7f;

	// Edging from a force balance (docs/physics/plan-2.md item 3; research 2.3, 3.1).
	bAutoEdge = true;
	MaxHeelDeg = 65.0f;
	LoadExtraHeelDeg = 25.0f;
	HeelResponse = 8.0f;
	CarveHeelDeg = MaxEdgeAngleDeg;  // the lean the carve was drawn with before it entered the balance
	FinAreaM2 = 0.013f;              // four fins 4.5 cm deep on a 7 cm mean chord
	RailAreaM2 = 0.08f;              // the immersed rail at full heel
	LateralLiftSlopePerRad = 2.5f;   // 2 to 3 /rad for these low aspect ratios
	LeewayStallDeg = 12.0f;
	TailWeightRailScale = 4.0f;      // weight fully back sinks the tail and buries the rail

	// The harness limits how far the board can point from the pull (docs/physics/plan-3.md item 3).
	MaxUpwindHeadingDeg = 50.0f;     // past the beam reach of the pull, away from the kite; tune 40 to 60
	bHarnessLimit = true;
	HarnessLeanRatePerS = 1.0f;      // a full lean against the hook in a second
	HarnessLeanReleaseRatePerS = 3.0f;
	HarnessLeanHeelDeg = 20.0f;      // the lean back drives the edge in, like the load
	HarnessYawRateDegPerS = 90.0f;   // a heading the pull has left behind comes back in well under a second

	// Jump tunables (Spec defaults)
	PopImpulseKgCmPerS = 21000.0f; // kg*cm/s: about 2.5 m/s from the legs alone; height comes from the kite
	EdgeReleaseSeconds = 0.0f;      // s: 0 = physics only; the lines' pull lifts the rider as a force once the edge lets go
	JumpMinSpeedKnots = 8.0f;   // 8 kn
	JumpMinEdgeInput = 0.4f;    // 0.4
	MaxJumpHeight = 500000.0f;  // 5 km: the base of the level's clouds
	LandingAbsorbDistanceCm = LandingMath::DefaultLandingAbsorbDistanceCm; // 45 cm standing: research 0.2 to 0.4 m of legs and immersion plus the water's give
	CrouchAbsorbBonus = 1.0f;           // a full crouch doubles it
	CrashLandingG = 10.0f; // measured landings are 4.2 to 5.5 g; 10 is a hard landing a rider can still stand, and what the trick grading uses
	HotLandingSinkMS = 6.0f;
	HotLandingKiteElevationDeg = 45.0f;
	CrashDecelDuration = 0.5f;  // 0.5 s
	CrashRespawnDelay = 1.0f;   // 1.0 s (total crash-to-reset: 1.5 s)
	MaxStepSeconds = 1.0f / 240.0f;
	MaxStepsPerUpdate = 48;

	CurrentDragRegime = EBoardDragRegime::Displacement;
	CurrentBoardState = EBoardState::Displacement;
	CurrentEdgeInput = 0.0f;
	CurrentWeightShift = 0.0f;
	SmoothedCarveInput = 0.0f;
	bLiftedByKite = false;
	AccumulatedExternalForce = FVector::ZeroVector;

	CurrentJumpHeight = 0.0f;
	CurrentJumpAirtime = 0.0f;
	CurrentJumpApexHeight = 0.0f;
	LastJumpApexHeight = 0.0f;
	LastJumpAirtime = 0.0f;
	BestJumpHeight = 0.0f;
	bLastLandingClean = true;
	bIsCrashing = false;
	CrashTimer = 0.0f;
	CrashInitialVelocity = FVector::ZeroVector;
	LandingStateTimer = 0.0f;
}

void UBoardMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	float DummyHeight = 0.0f;
	FVector DummyNormal = FVector::UpVector;
	SampleWaterSurface(GetActorLocation(), DummyHeight, DummyNormal);
}

void UBoardMovementComponent::SetWaterSurface(TSharedPtr<IKiteWaterSurface> InWaterSurface)
{
	WaterSurface = InWaterSurface;
}

TSharedPtr<IKiteWaterSurface> UBoardMovementComponent::GetWaterSurface() const
{
	return WaterSurface;
}

void UBoardMovementComponent::SampleWaterSurface(const FVector& Location, float& OutWaterHeight, FVector& OutWaterNormal) const
{
	if (WaterSurface.IsValid())
	{
		WaterSurface->SampleWaterSurface(Location, OutWaterHeight, OutWaterNormal);
		return;
	}

	OutWaterHeight = 0.0f;
	OutWaterNormal = FVector::UpVector;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!CachedWaterBodyComponent.IsValid())
	{
		for (TActorIterator<AWaterBody> It(World); It; ++It)
		{
			if (const AWaterBody* WaterBody = *It)
			{
				if (const UWaterBodyComponent* Comp = WaterBody->GetWaterBodyComponent())
				{
					CachedWaterBodyComponent = Comp;
					break;
				}
			}
		}
	}

	if (CachedWaterBodyComponent.IsValid())
	{
		WaterSurface = MakeShared<FKiteWaterBodySurface>(CachedWaterBodyComponent.Get());
		WaterSurface->SampleWaterSurface(Location, OutWaterHeight, OutWaterNormal);
	}
}

void UBoardMovementComponent::SampleWaterUnderBoard(const FVector& Location, float Yaw, float DeltaTime)
{
	// Centre, nose, tail, right rail, left rail, in the level frame of the heading.
	const FVector Forward = FRotator(0.0f, Yaw, 0.0f).Vector();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
	const float AlongCm = FMath::Max(WaterSampleAlongFraction, 0.0f) * BoardLengthCm;
	const float AcrossCm = FMath::Max(WaterSampleAcrossCm, 0.0f);
	const FVector Offsets[5] = { FVector::ZeroVector, Forward * AlongCm, -Forward * AlongCm, Right * AcrossCm, -Right * AcrossCm };
	float Heights[5] = {};
	for (int32 Index = 0; Index < 5; ++Index)
	{
		FVector Normal = FVector::UpVector;
		SampleWaterSurface(Location + Offsets[Index], Heights[Index], Normal);
		LastStepDebug.WaterSamplesCm[Index] = FVector(Location.X + Offsets[Index].X, Location.Y + Offsets[Index].Y, Heights[Index]);
	}

	// The least-squares plane z = h + a x + b y through the five, x along the heading and y across:
	// the points are symmetric about the centre, so h is their mean and each slope is the difference
	// across its own pair.
	const float HeightCm = (Heights[0] + Heights[1] + Heights[2] + Heights[3] + Heights[4]) / 5.0f;
	const float SlopeAlong = AlongCm > KINDA_SMALL_NUMBER ? (Heights[1] - Heights[2]) / (2.0f * AlongCm) : 0.0f;
	const float SlopeAcross = AcrossCm > KINDA_SMALL_NUMBER ? (Heights[3] - Heights[4]) / (2.0f * AcrossCm) : 0.0f;
	const FVector Gradient = Forward * SlopeAlong + Right * SlopeAcross;
	LastStepDebug.WaterHeightCm = HeightCm;
	LastStepDebug.WaterNormal = FVector(-Gradient.X, -Gradient.Y, 1.0f).GetSafeNormal();

	// How fast the surface rises under the board: its height change since the last step along the
	// path the board took. Without a path (the first step, or a board put somewhere), the slope times
	// the board's horizontal velocity.
	const float ExpectedTravelCm = Velocity.Size2D() * DeltaTime + WaterTrackJumpToleranceCm;
	const bool bTracked = bHasWaterTrack && DeltaTime > KINDA_SMALL_NUMBER && FVector::Dist2D(Location, LastWaterTrackLocation) <= ExpectedTravelCm;
	LastStepDebug.SurfaceVerticalSpeedCmS = bTracked ? (HeightCm - LastWaterTrackHeightCm) / DeltaTime : FVector::DotProduct(FVector(Velocity.X, Velocity.Y, 0.0f), Gradient);
	bHasWaterTrack = true;
	LastWaterTrackLocation = Location;
	LastWaterTrackHeightCm = HeightCm;
}

void UBoardMovementComponent::AddExternalForce(const FVector& Force)
{
	AccumulatedExternalForce += Force;
}

FString UBoardMovementComponent::JumpRejectReasonToString(EJumpRejectReason Reason)
{
	switch (Reason)
	{
	case EJumpRejectReason::NotPlaning:
		return TEXT("Get up on the board first");
	case EJumpRejectReason::TooSlow:
		return TEXT("Need more speed");
	case EJumpRejectReason::NotEdged:
		return TEXT("Edge harder");
	default:
		return FString();
	}
}

EJumpRejectReason UBoardMovementComponent::Jump()
{
	// A rider can always pop off the water: with or without an edge, fast or slow. What they
	// cannot do is pop when they are not on the board on the water: already in the air, in the
	// middle of a crash, or floating with the board under the surface.
	const bool bOnTheWater = CurrentBoardState == EBoardState::Planing || CurrentBoardState == EBoardState::Displacement;
	if (!bOnTheWater || bIsCrashing || IsFloating())
	{
		const FString ReasonStr = JumpRejectReasonToString(EJumpRejectReason::NotPlaning);
		UE_LOG(LogKiteSurf, Log, TEXT("Jump rejected: %s"), *ReasonStr);
		return EJumpRejectReason::NotPlaning;
	}
	const float SpeedKnots = KiteUnits::CmSToKnots(Velocity.Size2D());

	float UpwardKiteForce = 0.0f;
	if (const AActor* OwnerActor = GetOwner())
	{
		if (const UKiteComponent* KiteComp = OwnerActor->FindComponentByClass<UKiteComponent>())
		{
			UpwardKiteForce = FMath::Max(0.0f, KiteComp->GetLineForce().Z);
		}
	}

	// The legs push off harder with the weight back and from a loaded crouch, and less from a
	// board that is part sunk.
	const float OnSurface = FloatSubmersionCm > 0.0f ? FMath::Clamp(1.0f - CurrentFloatDepthCm / FloatSubmersionCm, 0.0f, 1.0f) : 1.0f;
	const float PopImpulse = PopImpulseKgCmPerS * OnSurface * (1.0f + TailWeightPopBonus * FMath::Max(-CurrentWeightShift, 0.0f)) * (1.0f + LoadPopBonus * LoadAmount);
	LoadAmount = 0.0f;
	const float Impulse = PopImpulse + EdgeReleaseSeconds * UpwardKiteForce;
	const float EffectiveMass = FMath::Max(MassKg, 1.0f);
	const float VerticalDeltaV = Impulse / EffectiveMass;

	Velocity.Z = FMath::Max(Velocity.Z + VerticalDeltaV, VerticalDeltaV);

	BeginAirborne(true);

	UE_LOG(LogKiteSurf, Log, TEXT("Board Jump initiated: Speed=%.1f kn, Edge=%.2f, KiteLiftZ=%.1f, Impulse=%.1f, VZ=%.1f cm/s"),
		SpeedKnots, CurrentEdgeInput, UpwardKiteForce, Impulse, Velocity.Z);

	return EJumpRejectReason::None;
}

void UBoardMovementComponent::BeginAirborne(bool bPopped)
{
	bLiftedByKite = !bPopped;
	CurrentBoardState = EBoardState::Airborne;
	CurrentJumpAirtime = 0.0f;
	CurrentJumpHeight = 0.0f;
	CurrentJumpApexHeight = 0.0f;
	CurrentJumpDistance = 0.0f;
	JumpStartLocation = UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
	LandingStateTimer = 0.0f;

	// The take-off, for the trick tracker and anything bound. A pop comes between steps and a kite
	// lift-off inside one; either way the jump's airtime counts from here, so the landing comes
	// LastJumpAirtime after LastTakeoffTimeSeconds.
	++TakeoffCount;
	bLastTakeoffPopped = bPopped;
	LastTakeoffTimeSeconds = SimTimeSeconds;
	CurrentJumpApexTimeSeconds = SimTimeSeconds;
	bJumpRising = false; // a lift-off can start level: the apex waits for the board to rise first
	LastApexEventHeightCm = -1.0f;
	OnBoardTakeoff.Broadcast(bPopped);
}

void UBoardMovementComponent::NoteJumpEnd(float LandingAngleDeg)
{
	LastLandingAngleDeg = LandingAngleDeg;
}

void UBoardMovementComponent::SetBoardSize(EBoardSize InSize)
{
	BoardSize = KiteGear::BoardSizeFromIndex(static_cast<int32>(InSize));
	const FBoardSizeTraits Traits = KiteGear::GetTraits(BoardSize);
	// Reference 138 cm board: the constructor's values.
	PopImpulseKgCmPerS = 21000.0f * Traits.PopScale;
	PlaningThresholdCmS = 400.0f * Traits.PlaningSpeedScale;
	LowSpeedPivotMaxSpeedCmS = PlaningThresholdCmS;
	PlaningDragKgPerS = ReferencePlaningDragKgPerS * Traits.PlaningDragScale;
	PlaningQuadraticDragKgPerCm = ReferencePlaningQuadraticDragKgPerCm * Traits.PlaningDragScale;
	PlaningTrimHumpSpeedCmS = 600.0f * Traits.PlaningSpeedScale; // the hump is where this board planes
	RailAreaM2 = 0.08f * Traits.GripScale; // a longer board has more rail in the water
	CarveTurnRate = 60.0f * Traits.TurnRateScale;
}

float UBoardMovementComponent::GetPlaningTrimDeg(float SpeedCmS) const
{
	// Savitsky: the hull trims highest just past the hump and flattens out with speed.
	const float HumpSpanCmS = PlaningTrimHumpSpeedCmS - PlaningThresholdCmS;
	const float PastHump = HumpSpanCmS > KINDA_SMALL_NUMBER ? FMath::Clamp((SpeedCmS - PlaningThresholdCmS) / HumpSpanCmS, 0.0f, 1.0f) : 1.0f;
	return FMath::Max(FMath::Lerp(PlaningTrimHumpDeg, PlaningTrimDeg, PastHump), 0.0f);
}

float UBoardMovementComponent::GetFloatDepthForSpeed(float SpeedCmS) const
{
	// Fully sunk below FloatUntilSpeedFraction of planing speed, on the surface at planing speed.
	const float Planing = FMath::SmoothStep(PlaningThresholdCmS * FloatUntilSpeedFraction, PlaningThresholdCmS, SpeedCmS);
	return FloatSubmersionCm * (1.0f - Planing);
}

float UBoardMovementComponent::GetFloatDepthForSpeedAndPull(float SpeedCmS, float PullN) const
{
	// The pull lifts the rider out of the water before the board is moving fast enough to carry them:
	// the share it lifts grows with the tension, to all of it at FloatRiseTensionN.
	const float Lifted = FloatRiseTensionN > KINDA_SMALL_NUMBER ? FMath::Clamp(PullN / FloatRiseTensionN, 0.0f, 1.0f) : 0.0f;
	return GetFloatDepthForSpeed(SpeedCmS) * (1.0f - Lifted);
}

void UBoardMovementComponent::SetWeightShift(float Value)
{
	CurrentWeightShift = FMath::Clamp(Value, -1.0f, 1.0f);
}

void UBoardMovementComponent::SetEdgeInput(float Value)
{
	CurrentEdgeInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

float UBoardMovementComponent::GetForwardSpeed() const
{
	if (!UpdatedComponent)
	{
		return Velocity.X;
	}
	// Along the board's heading over the water, whatever its heel and pitch.
	return FVector::DotProduct(Velocity, FRotator(0.0f, UpdatedComponent->GetComponentRotation().Yaw, 0.0f).Vector());
}

float UBoardMovementComponent::GetLateralSpeed() const
{
	if (!UpdatedComponent)
	{
		return Velocity.Y;
	}
	// Across the board's heading over the water, whatever its heel: positive to its right.
	const FVector LevelForward = FRotator(0.0f, UpdatedComponent->GetComponentRotation().Yaw, 0.0f).Vector();
	return FVector::DotProduct(Velocity, FVector::CrossProduct(FVector::UpVector, LevelForward));
}

void UBoardMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	Simulate(DeltaTime);
}

void UBoardMovementComponent::Simulate(float DeltaTime)
{
	if (DeltaTime <= 0.0f)
	{
		return;
	}
	const int32 NumSteps = FMath::Clamp(FMath::CeilToInt(DeltaTime / FMath::Max(MaxStepSeconds, KINDA_SMALL_NUMBER)), 1, FMath::Max(MaxStepsPerUpdate, 1));
	const float StepSeconds = DeltaTime / NumSteps;
	// A force added before this update is held for the whole of it.
	const FVector HeldForce = AccumulatedExternalForce;
	for (int32 Step = 0; Step < NumSteps; ++Step)
	{
		AccumulatedExternalForce = HeldForce;
		StepBoard(StepSeconds);
	}
}

void UBoardMovementComponent::StepBoard(float StepSeconds)
{
	const float DeltaTime = StepSeconds;
	SimTimeSeconds += FMath::Max(StepSeconds, 0.0f);
	LastStepDebug = FBoardStepDebug();
	// The force added for this step is spent in this step, whatever the board does with it. A crash's
	// scripted stop owns the rider's motion, and before plan-3 item 1 it returned without spending the
	// lines' pull: 1.5 s of a powered kite was saved up and came out in the first step after the reset,
	// an impulse of 20 to 34 m/s that threw the rider 13 to 30 m up and 47 to 80 m away.
	const FVector StepExternalForce = AccumulatedExternalForce;
	AccumulatedExternalForce = FVector::ZeroVector;
	if (!ShouldSkipUpdate(DeltaTime) && UpdatedComponent)
	{
		const FVector Location = UpdatedComponent->GetComponentLocation();
		const FRotator Rotation = UpdatedComponent->GetComponentRotation();
		const FVector Forward = UpdatedComponent->GetForwardVector();

		// The water under the board: a plane fitted to five samples, and how fast it is rising here.
		SampleWaterUnderBoard(Location, Rotation.Yaw, DeltaTime);

		// 0. Crash recovery handling
		if (bIsCrashing)
		{
			CrashTimer += DeltaTime;
			if (CrashTimer <= CrashDecelDuration)
			{
				const float DecelAlpha = FMath::Clamp(CrashTimer / CrashDecelDuration, 0.0f, 1.0f);
				Velocity = FMath::Lerp(CrashInitialVelocity, FVector::ZeroVector, DecelAlpha);
			}
			else
			{
				Velocity = FVector::ZeroVector;
			}

			if (CrashTimer >= CrashDecelDuration + CrashRespawnDelay)
			{
				// Rider respawns upright after 1.5 s at crash position with 8 kn on previous tack
				ResetToTack(8.0f);
				UE_LOG(LogKiteSurf, Log, TEXT("Rider respawned upright after crash recovery"));
			}

			const FVector MoveDelta = Velocity * DeltaTime;
			FHitResult Hit(1.0f);
			SafeMoveUpdatedComponent(MoveDelta, UpdatedComponent->GetComponentRotation(), true, Hit);
			UpdateComponentVelocity();
			return;
		}

		// 0b. Landing state timer countdown
		if (LandingStateTimer > 0.0f)
		{
			LandingStateTimer -= DeltaTime;
			if (LandingStateTimer <= 0.0f)
			{
				if (Velocity.Size2D() >= PlaningThresholdCmS)
				{
					CurrentBoardState = EBoardState::Planing;
					CurrentDragRegime = EBoardDragRegime::Planing;
				}
				else
				{
					CurrentBoardState = EBoardState::Displacement;
					CurrentDragRegime = EBoardDragRegime::Displacement;
				}
			}
		}

		// 1. The water under the board (sampled above, before the crash recovery).
		const float WaterHeight = LastStepDebug.WaterHeightCm;
		const FVector WaterNormal = LastStepDebug.WaterNormal;
		const float SurfaceVerticalSpeed = LastStepDebug.SurfaceVerticalSpeedCmS;

		bool bIsAirborne = (CurrentBoardState == EBoardState::Airborne);

		// Without planing speed the board does not carry the rider: they float, sunk to the chest. The
		// kite's pull lifts them onto the board, and they rise onto the surface as the board gets up to
		// speed (the water start). A rider in the air comes down onto the surface first and sinks from
		// there.
		const float PullN = (StepExternalForce / KiteUnits::UnrealForcePerN).Size();
		const float TargetFloatDepthCm = bIsAirborne ? 0.0f : GetFloatDepthForSpeedAndPull(Velocity.Size2D(), PullN);
		CurrentFloatDepthCm = FMath::FInterpTo(CurrentFloatDepthCm, TargetFloatDepthCm, DeltaTime, FloatResponse);
		const float RideHeight = WaterHeight - CurrentFloatDepthCm;

		const float HeightAboveWater = Location.Z - WaterHeight;
		bool bHydrodynamicsDisabled = bIsAirborne && (HeightAboveWater > WaterContactHeightCm);

		// Touchdown: the absorber takes the sink into the water out over the absorb distance (the legs
		// and the board's immersion), and stops once the board moves with the surface. A board that left
		// the water without a jump (over a swell, or a hop) and comes back down is absorbed the same way.
		const bool bInWaterContact = HeightAboveWater <= WaterContactHeightCm;
		const float RelativeSinkCmS = SurfaceVerticalSpeed - Velocity.Z; // positive sinking into the water
		if (bAbsorbing && (RelativeSinkCmS <= AbsorbDoneSinkCmS || bIsAirborne))
		{
			bAbsorbing = false;
		}
		if (!bIsAirborne && !bAbsorbing && bInWaterContact && !bWasInWaterContact && RelativeSinkCmS > 0.0f)
		{
			BeginTouchdownAbsorb(RelativeSinkCmS);
		}
		bWasInWaterContact = bInWaterContact;

		if (bIsAirborne)
		{
			CurrentJumpAirtime += DeltaTime;
			CurrentJumpHeight = FMath::Max(0.0f, HeightAboveWater);
			if (CurrentJumpHeight > CurrentJumpApexHeight)
			{
				CurrentJumpApexHeight = CurrentJumpHeight;
				// The height is where the last step left the board, at the start of this one.
				CurrentJumpApexTimeSeconds = SimTimeSeconds - FMath::Max(DeltaTime, 0.0f);
			}
			CurrentJumpDistance = FVector::Dist2D(Location, JumpStartLocation);

			// Clamp apex at MaxJumpHeight (the cloud base)
			if (Location.Z >= WaterHeight + MaxJumpHeight)
			{
				FVector ClampedLocation = Location;
				ClampedLocation.Z = WaterHeight + MaxJumpHeight;
				UpdatedComponent->SetWorldLocation(ClampedLocation);
				Velocity.Z = FMath::Min(Velocity.Z, 0.0f);
			}

			// Apex: the first step that is no longer rising after rising, at a new highest point
			// (a kite yank after the rider started down can make a second, higher one).
			if (Velocity.Z > 0.0f)
			{
				bJumpRising = true;
			}
			else if (bJumpRising)
			{
				bJumpRising = false;
				if (CurrentJumpApexHeight > LastApexEventHeightCm)
				{
					LastApexEventHeightCm = CurrentJumpApexHeight;
					++ApexCount;
					OnBoardApex.Broadcast(CurrentJumpApexHeight);
				}
			}
		}

		// 2. Setup total forces
		FVector TotalForce = StepExternalForce;
		const FVector ExternalForceN = StepExternalForce / KiteUnits::UnrealForcePerN;
		const FVector ExternalForce2D(StepExternalForce.X, StepExternalForce.Y, 0.0f);

		const float GravityZ = -KiteUnits::GravityCmS2; // the same g the kite uses, whatever the world settings say
		const float GravityForceZ = MassKg * GravityZ; // negative in kg*cm/s^2
		TotalForce.Z += GravityForceZ;

		// In the air the rider and board are a body in the wind: it drags them along with it. On
		// the water the hull's drag and grip dwarf it, and are the model there.
		if (bIsAirborne)
		{
			const FVector AirDragForce = ComputeAirDragForce(Location, Velocity);
			TotalForce += AirDragForce;
			LastStepDebug.AirDragN = AirDragForce / KiteUnits::UnrealForcePerN;
		}

		// The load: held, the rider sinks into a crouch with their weight over the back of the
		// board and drives the edge in. It builds over a moment and lets go quickly. In the air the
		// same crouch readies the legs for the landing: it lengthens the touchdown's absorb distance.
		const bool bCanLoad = bLoadHeld && !bIsCrashing && !IsFloating();
		LoadAmount = FMath::Clamp(LoadAmount + (bCanLoad ? LoadRatePerSec : -LoadReleaseRatePerSec) * DeltaTime, 0.0f, 1.0f);

		// The kite lifts the rider off when it pulls up harder than they weigh: sending the kite
		// overhead or looping it does this without a pop. Crouched and loaded, the rider hangs on to
		// LoadHoldBonus body weights more until they let go (docs/physics/plan-2.md item 3).
		const float LiftoffFactor = 1.0f + LoadHoldBonus * LoadAmount;
		if (!bIsAirborne && CurrentBoardState != EBoardState::Landing && TotalForce.Z > -GravityForceZ * (LiftoffFactor - 1.0f))
		{
			BeginAirborne(false);
			bIsAirborne = true;
			Velocity.Z = FMath::Max(Velocity.Z, 0.0f);
			UE_LOG(LogKiteSurf, Log, TEXT("Board lifted off by the kite: upward force %.0f N against %.0f N of weight"), KiteUnits::UnrealForceToN(TotalForce.Z - GravityForceZ), KiteUnits::UnrealForceToN(-GravityForceZ));
		}

		// 3. Buoyancy & Vertical Dynamics (disabled while above water surface + 10 cm)
		// How far below its riding height the board is: that height is the surface when planing
		// and the floating depth when not.
		const float Submersion = RideHeight - Location.Z;
		float SupportForceZ = 0.0f; // the water holding the board up, kg*cm/s^2
		if (!bHydrodynamicsDisabled && Submersion >= -BuoyancyReachCm)
		{
			// A damped spring about the ride height, set by its frequency and damping ratio.
			const float BuoyancyBalance = -GravityForceZ; // exactly balances gravity at rest
			const float OmegaRadS = 2.0f * PI * BuoyancyNaturalFrequencyHz;
			const float SpringForceZ = EffectiveMassForBuoyancy() * OmegaRadS * OmegaRadS * Submersion;
			// The damping acts on the board's vertical speed relative to the surface under it, which on
			// a swell rises and falls as the board rides over it.
			const float DampingForceZ = -2.0f * BuoyancyDampingRatio * EffectiveMassForBuoyancy() * OmegaRadS * (Velocity.Z - SurfaceVerticalSpeed);

			float BuoyancyForceZ = BuoyancyBalance + SpringForceZ + DampingForceZ;
			const float MaxBuoyancyForce = KiteUnits::NToUnrealForce(BuoyancyN);
			BuoyancyForceZ = FMath::Clamp(BuoyancyForceZ, 0.0f, MaxBuoyancyForce);

			SupportForceZ += BuoyancyForceZ;
		}

		// 4. Horizontal Hydrodynamics (Displacement vs Planing, Edging, Lift)
		const float Speed2D = Velocity.Size2D();
		const float ForwardSpeed = FVector::DotProduct(Velocity, Forward);

		const bool bPlaning = (Speed2D >= PlaningThresholdCmS);
		if (!bIsAirborne && CurrentBoardState != EBoardState::Landing)
		{
			if (bPlaning && CurrentDragRegime != EBoardDragRegime::Planing)
			{
				CurrentDragRegime = EBoardDragRegime::Planing;
				CurrentBoardState = EBoardState::Planing;
				UE_LOG(LogKiteSurf, Log, TEXT("Board transitioned to Planing drag regime (Speed: %.1f cm/s >= Threshold: %.1f cm/s)"), Speed2D, PlaningThresholdCmS);
			}
			else if (!bPlaning && CurrentDragRegime != EBoardDragRegime::Displacement)
			{
				CurrentDragRegime = EBoardDragRegime::Displacement;
				CurrentBoardState = EBoardState::Displacement;
				UE_LOG(LogKiteSurf, Log, TEXT("Board transitioned to Displacement drag regime (Speed: %.1f cm/s < Threshold: %.1f cm/s)"), Speed2D, PlaningThresholdCmS);
			}
		}

		// The drag and the side force are integrated exactly over the step (below), so they never
		// overshoot however long the step is. Here only the forces are added: the planing lift, and
		// the sideways part of the heeled board's normal force.
		const float EffectiveMass = FMath::Max(MassKg, 1.0f);
		float DragRatePerS = 0.0f;   // linear drag as a rate: a in dv/dt = -a v - b v^2
		float DragRatePerCm = 0.0f;  // quadratic drag as a rate: b

		// A floating rider is pulled through the water body first: 0.5 rho_w A v^2 at any speed,
		// blending in from half the floating depth to all of it (docs/physics/plan-2.md item 3d). It
		// goes only as the body rises, lifted by the pull or carried by the board's speed: a sunk body
		// dragged faster keeps its drag, so the speed a kite can drag it at is bounded (plan-3.md item
		// 1; until then it faded out by 2 m/s, and a dragged rider had nothing holding them back). As a
		// rate on the speed in cm/s, b = 0.5 rho_w A / (m * 100 cm/m).
		const float SunkFraction = FloatSubmersionCm > 0.0f ? CurrentFloatDepthCm / FloatSubmersionCm : 0.0f;
		const float DepthBlend = FMath::Clamp(2.0f * SunkFraction - 1.0f, 0.0f, 1.0f);
		const float FloatingDragRatePerCm = 0.5f * KiteUnits::WaterDensityKgM3 * FMath::Max(FloatingDragAreaM2, 0.0f) * DepthBlend / (EffectiveMass * KiteUnits::CmPerM);
		if (!bHydrodynamicsDisabled)
		{
			if (!bPlaning)
			{
				// Displacement regime: quadratic + linear drag so depowering stops (< 2 kn) within 5s
				DragRatePerS = DisplacementDragKgPerS / EffectiveMass;
				DragRatePerCm = DisplacementQuadraticDragKgPerCm / EffectiveMass + FloatingDragRatePerCm;
			}
			else
			{
				// Planing regime: linear drag + high-speed form/spray drag
				// A sunk tail drags more; weight forward flattens the board and frees it up. A rider
				// yanked past planing speed before their body has come up still drags it.
				const float EdgeDragScale = 1.0f + TailWeightDrag * FMath::Max(-CurrentWeightShift, 0.0f) - NoseWeightDragSaving * FMath::Max(CurrentWeightShift, 0.0f);
				DragRatePerS = PlaningDragKgPerS * EdgeDragScale / EffectiveMass;
				DragRatePerCm = PlaningQuadraticDragKgPerCm * EdgeDragScale / EffectiveMass + FloatingDragRatePerCm;

				// Hydrodynamic lift raising the board with surface contact falloff
				const float SurfaceContact = FMath::Clamp((Submersion + WaterContactHeightCm) / (WaterContactHeightCm + PlaningLiftFullDepthCm), 0.0f, 1.0f);
				const float PlaningLift = PlaningLiftKgPerS * (Speed2D - PlaningThresholdCmS) * SurfaceContact;
				SupportForceZ += PlaningLift;
			}
		}

		// Taking a touchdown's sink out, the water and the legs hold the board up with whatever gives
		// the absorber's constant deceleration of the sink, and never less than the buoyancy and the
		// planing lift would; in the last step only as much as stops it moving into the water.
		if (bAbsorbing && !bHydrodynamicsDisabled)
		{
			const float DecelCmS2 = FMath::Min(AbsorbDecelCmS2, FMath::Max(RelativeSinkCmS, 0.0f) / FMath::Max(DeltaTime, KINDA_SMALL_NUMBER));
			const float AbsorberForceZ = FMath::Max(MassKg * (KiteUnits::GravityCmS2 + DecelCmS2) - ExternalForceN.Z * KiteUnits::UnrealForcePerN, 0.0f);
			SupportForceZ = FMath::Max(SupportForceZ, AbsorberForceZ);
		}
		// The loaded hold (docs/physics/plan-2.md item 3): crouched, the rider's legs keep the board on
		// the water against the lines until they pull up harder than m g (1 + LoadHoldBonus * load), when
		// the lift-off rule (above) lets it go. LoadHoldHeightCm above its ride height the legs hold the
		// board down with up to LoadHoldBonus * load * m g, and stop it rising there: its upward speed
		// relative to the surface is taken out in the step, as the old 20 cm clamp did for every board.
		if (!bIsAirborne && !bAbsorbing && !bHydrodynamicsDisabled && LoadAmount > 0.0f && -Submersion >= LoadHoldHeightCm)
		{
			const float HoldBudgetZ = FMath::Max(LoadHoldBonus, 0.0f) * LoadAmount * -GravityForceZ;
			const float StopRiseZ = MassKg * FMath::Max(Velocity.Z - SurfaceVerticalSpeed, 0.0f) / FMath::Max(DeltaTime, KINDA_SMALL_NUMBER);
			SupportForceZ -= FMath::Clamp(TotalForce.Z + SupportForceZ, 0.0f, HoldBudgetZ) + StopRiseZ;
		}
		TotalForce.Z += SupportForceZ;
		LastStepDebug.WaterVerticalForceN = KiteUnits::UnrealForceToN(SupportForceZ);
		LastStepDebug.bAbsorbing = bAbsorbing;

		// Edging is a force balance (docs/physics/plan-2.md item 3): the rider heels the board against
		// the pull across it, and the water's normal force on the heeled board carries that pull; the
		// fins and the rail turn the velocity towards the board's axis (after the forces, below).
		// Nothing drives the board but the kite.
		const FVector LevelForward = FRotator(0.0f, Rotation.Yaw, 0.0f).Vector();
		const FVector LevelRight = FVector::CrossProduct(FVector::UpVector, LevelForward);
		const bool bStandingOnWater = !bIsAirborne && !bHydrodynamicsDisabled;
		TotalForce += UpdateHeelAndNormalSideForce(DeltaTime, ExternalForceN, LevelRight, bStandingOnWater);

		// On the plane the normal force leans back by the trim: its horizontal part, N tan(trim), is
		// the pressure drag (research 2.2), and N grows as 1 / cos(heel), so carrying the edge costs
		// drag. The trim is highest just over the planing hump and falls as the board speeds up
		// (docs/physics/plan-2.md item 3b).
		float DragDecelCmS2 = 0.0f;
		if (!bHydrodynamicsDisabled && bPlaning)
		{
			LastStepDebug.TrimDeg = GetPlaningTrimDeg(Speed2D);
			LastStepDebug.PressureDragN = LastStepDebug.NormalForceN * FMath::Tan(FMath::DegreesToRadians(LastStepDebug.TrimDeg));
			DragDecelCmS2 = KiteUnits::NToUnrealForce(LastStepDebug.PressureDragN) / EffectiveMass;
		}

		// 5. Velocity Integration: the forces, then the drag and the side force as exact solutions over
		// the step, which is the same at any step length.
		Velocity += TotalForce / EffectiveMass * DeltaTime;
		if (!bHydrodynamicsDisabled)
		{
			ApplyWaterDragAndSideForce(DeltaTime, DragDecelCmS2, DragRatePerS, DragRatePerCm, LevelForward);
		}

		// A board on the water goes no faster than MaxBoardSpeed. In the air only the air drags on
		// the rider: the kite carries them downwind until the wind they feel has dropped, which is
		// what brings a rider lofted in a storm back down.
		const float MaxSpeedCmS = GetMaxBoardSpeedCmS();
		if (!bIsAirborne && Velocity.Size2D() > MaxSpeedCmS)
		{
			const FVector Clamped2D = Velocity.GetSafeNormal2D() * MaxSpeedCmS;
			Velocity.X = Clamped2D.X;
			Velocity.Y = Clamped2D.Y;
		}

		// Ensure no NaN/Inf
		ensureAlwaysMsgf(!Velocity.ContainsNaN(), TEXT("BoardMovementComponent: Velocity contains NaN or Inf"));

		// 6. Orientation Alignment: keep roll/pitch aligned with water normal + edge angle
		SmoothedCarveInput = FMath::FInterpTo(SmoothedCarveInput, CurrentEdgeInput, DeltaTime, CarveResponse);
		FRotator TargetRotation = Rotation;

		// The harness (docs/physics/plan-3.md item 3): standing on the board on the water with the lines
		// taut and pulling across the water, the heading is held within MaxUpwindHeadingDeg upwind of
		// the beam reach of the pull. The pull's direction is the line force's, the board's only
		// external force. Never in the air, where the rider attitude or the kinematic spin owns the
		// heading.
		const UKiteComponent* HarnessKite = GetOwner() ? GetOwner()->FindComponentByClass<UKiteComponent>() : nullptr;
		const bool bHarnessActive = bHarnessLimit && !bIsAirborne && !bHydrodynamicsDisabled && !IsFloating() && HarnessKite && HarnessKite->AreLinesTaut()
			&& ExternalForce2D.SizeSquared() > FMath::Square(KiteUnits::NToUnrealForce(HarnessMinHorizontalPullN));
		const float PullYawDeg = bHarnessActive ? FMath::RadiansToDegrees(FMath::Atan2(ExternalForce2D.Y, ExternalForce2D.X)) : 0.0f;
		bool bTurnedByCarve = false;
		bool bSwitchedEnds = false;
		bAirBoardQuatInUse = false;
		if (bIsAirborne && bAirAttitudeActive)
		{
			// The rider's attitude turns the strapped board (T1.2): it is drawn and landed with its full
			// orientation, and the physics root keeps only its heading for the water and the landing's
			// course. A board pointing nearly straight up or down has no heading to speak of: the last
			// one is held.
			AirBoardQuat = AirAttitudeBoardQuat.GetNormalized();
			bAirBoardQuatInUse = true;
			const FVector Nose = AirBoardQuat.GetAxisX();
			const FVector Nose2D(Nose.X, Nose.Y, 0.0f);
			constexpr float MinHeadingNoseLength = 0.2f;
			const float HeadingYaw = Nose2D.Size() >= MinHeadingNoseLength ? FMath::RadiansToDegrees(FMath::Atan2(Nose2D.Y, Nose2D.X)) : Rotation.Yaw;
			TargetRotation = FRotator(0.0f, HeadingYaw, 0.0f);
		}
		else if (bIsAirborne)
		{
			// Without the rider attitude: in the air the carve input spins the board. Left alone, the
			// rider brings it back in line with the direction of travel (either way round) for the landing.
			if (FMath::Abs(CurrentEdgeInput) > 0.05f)
			{
				TargetRotation.Yaw = FRotator::NormalizeAxis(Rotation.Yaw + CurrentEdgeInput * AirSpinRate * DeltaTime);
			}
			else if (Speed2D > 100.0f)
			{
				const float VelocityHeading = FMath::RadiansToDegrees(FMath::Atan2(Velocity.Y, Velocity.X));
				float YawError = FRotator::NormalizeAxis(VelocityHeading - Rotation.Yaw);
				if (FMath::Abs(YawError) > 90.0f)
				{
					YawError = FRotator::NormalizeAxis(YawError + 180.0f);
				}
				const float MaxStep = AirSpinRate * DeltaTime;
				TargetRotation.Yaw = FRotator::NormalizeAxis(Rotation.Yaw + FMath::Clamp(YawError, -MaxStep, MaxStep));
			}
			TargetRotation.Pitch = FMath::FInterpTo(Rotation.Pitch, -CurrentWeightShift * AirWeightShiftPitchDeg, DeltaTime, 4.0f);
			TargetRotation.Roll = FMath::FInterpTo(Rotation.Roll, 0.0f, DeltaTime, 4.0f);
		}
		else
		{
			// Slope of the water along and across the board's heading. Measured against the level
			// heading, not the board's current tilt, or the tilt would feed back into itself.
			const float SurfacePitch = FMath::RadiansToDegrees(FMath::Atan2(-FVector::DotProduct(WaterNormal, LevelForward), FVector::DotProduct(WaterNormal, FVector::UpVector)));
			const float SurfaceRoll = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(WaterNormal, LevelRight), FVector::DotProduct(WaterNormal, FVector::UpVector)));

			// Positive pitch is nose up: weight on the tail lifts the nose.
			TargetRotation.Pitch = FMath::Clamp(SurfacePitch - CurrentWeightShift * WeightShiftPitchDeg, -20.0f, 20.0f);
			// The heel lifts the rail on the kite's side; it carries the carve's lean into the turn too.
			TargetRotation.Roll = FMath::Clamp(SurfaceRoll - HeelDeg, -MaxHeelDeg, MaxHeelDeg);

			// A twin-tip rides either way: once it is moving tail-first, the tail becomes the nose.
			if (ForwardSpeed < -SwitchStanceSpeedCmS && CurrentBoardState != EBoardState::Landing)
			{
				TargetRotation.Yaw = FRotator::NormalizeAxis(Rotation.Yaw + 180.0f);
				TargetRotation.Pitch = -TargetRotation.Pitch;
				TargetRotation.Roll = -TargetRotation.Roll;
				HeelDeg = -HeelDeg; // the same rail in the water, seen from the new nose
				BalanceHeelDeg = -BalanceHeelDeg;
				bSwitchedEnds = true;
			}
			// Edging changes board heading relative to velocity
			else if (FMath::Abs(ForwardSpeed) > 50.0f && FMath::Abs(SmoothedCarveInput) > 0.02f)
			{
				const float VelocityHeading = FMath::RadiansToDegrees(FMath::Atan2(Velocity.Y, Velocity.X));
				const float DesiredHeading = FRotator::NormalizeAxis(VelocityHeading + SmoothedCarveInput * MaxEdgeAngleDeg);
				const float DeltaYaw = FRotator::NormalizeAxis(DesiredHeading - Rotation.Yaw);
				const float MaxTurnStep = CarveTurnRate * FMath::Abs(SmoothedCarveInput) * FMath::Clamp(Speed2D / PlaningThresholdCmS, 0.5f, 1.0f) * DeltaTime;
				TargetRotation.Yaw = FRotator::NormalizeAxis(Rotation.Yaw + FMath::Clamp(DeltaYaw, -MaxTurnStep, MaxTurnStep));
				bTurnedByCarve = true;
			}
			// Off the plane the rider can pivot the board freely, and lines it up for the kite to
			// pull it back onto the plane (the water-start position).
			else if (Speed2D < LowSpeedPivotMaxSpeedCmS && ExternalForce2D.SizeSquared() > FMath::Square(KiteUnits::NToUnrealForce(LowSpeedPivotMinForceN)))
			{
				// Across the wind on the kite's side is where the pull drives the board best; pointing
				// straight at the kite would just drag the rider downwind after it.
				FVector PivotDirection = ExternalForce2D;
				if (const AActor* OwnerActor = GetOwner())
				{
					if (const UKiteComponent* KiteComp = OwnerActor->FindComponentByClass<UKiteComponent>())
					{
						// A pull straight downwind has no side to choose: follow it.
						const FVector Crosswind = FVector::CrossProduct(FVector::UpVector, KiteComp->GetDownwindDir());
						const float SidewaysPull = FVector::DotProduct(ExternalForce2D, Crosswind);
						if (FMath::Abs(SidewaysPull) >= 0.3f * ExternalForce2D.Size())
						{
							PivotDirection = Crosswind * (SidewaysPull >= 0.0f ? 1.0f : -1.0f);
						}
					}
				}
				const float PullHeading = FMath::RadiansToDegrees(FMath::Atan2(PivotDirection.Y, PivotDirection.X));
				float YawError = FRotator::NormalizeAxis(PullHeading - Rotation.Yaw);
				// Either end of a twin-tip will do, the nearer one; but in the harness not a nose pointing
				// further from the pull than the body can twist, which the board could only reach by
				// turning away from the kite (one of the two always points towards it).
				const bool bNoseThereAllowed = !bHarnessActive || UpwindOfBeamForHeading(PullHeading, PullYawDeg) <= MaxUpwindHeadingDeg;
				const bool bTailThereAllowed = !bHarnessActive || UpwindOfBeamForHeading(PullHeading + 180.0f, PullYawDeg) <= MaxUpwindHeadingDeg;
				if ((FMath::Abs(YawError) > 90.0f && bTailThereAllowed) || !bNoseThereAllowed)
				{
					YawError = FRotator::NormalizeAxis(YawError + 180.0f);
				}
				// Full rate when stopped, easing to a fifth just below planing speed, so a slow drift
				// in the wrong direction still comes round.
				const float PivotScale = FMath::Clamp(1.0f - Speed2D / LowSpeedPivotMaxSpeedCmS, 0.2f, 1.0f);
				const float MaxStep = LowSpeedPivotRate * PivotScale * DeltaTime;
				TargetRotation.Yaw = FRotator::NormalizeAxis(Rotation.Yaw + FMath::Clamp(YawError, -MaxStep, MaxStep));
			}
		}

		// The harness holds whatever turned the board this step within the limit (a twin-tip swapping
		// ends keeps the end it travels on, so it is left alone); the carve input pushed against it
		// becomes the rider's lean back against the hook, which builds up to the input's size and lets
		// go once the input is no longer held against the limit.
		bool bAgainstHarness = false;
		if (bHarnessActive)
		{
			if (!bSwitchedEnds)
			{
				TargetRotation.Yaw = LimitTurnByHarness(Rotation.Yaw, TargetRotation.Yaw, PullYawDeg, DeltaTime, bAgainstHarness);
				bAgainstHarness &= bTurnedByCarve;
			}
			LastStepDebug.bHarnessActive = true;
			LastStepDebug.UpwindOfBeamDeg = UpwindOfBeamForHeading(TargetRotation.Yaw, PullYawDeg);
		}
		// The rate the carve turned the board at, after the harness: the rider's lean into the turn
		// in the next step is the one this turn needs.
		CarveYawRateDegPerS = (bTurnedByCarve && DeltaTime > KINDA_SMALL_NUMBER) ? FRotator::NormalizeAxis(TargetRotation.Yaw - Rotation.Yaw) / DeltaTime : 0.0f;
		LastStepDebug.bAgainstHarness = bAgainstHarness;
		const float LeanTarget = bAgainstHarness ? FMath::Abs(CurrentEdgeInput) : 0.0f;
		HarnessLeanAmount = HarnessLeanAmount < LeanTarget
			? FMath::Min(HarnessLeanAmount + FMath::Max(HarnessLeanRatePerS, 0.0f) * DeltaTime, LeanTarget)
			: FMath::Max(HarnessLeanAmount - FMath::Max(HarnessLeanReleaseRatePerS, 0.0f) * DeltaTime, LeanTarget);

		// 7. Apply movement via SafeMoveUpdatedComponent
		const FVector MoveDelta = Velocity * DeltaTime;
		FHitResult Hit(1.0f);
		SafeMoveUpdatedComponent(MoveDelta, TargetRotation, true, Hit);

		if (Hit.IsValidBlockingHit())
		{
			SlideAlongSurface(MoveDelta, 1.0f - Hit.Time, Hit.Normal, Hit);
		}

		const FVector PostMoveLocation = UpdatedComponent->GetComponentLocation();

		// 8. Landing: the board comes down to the water, sinking into it (relative to the surface,
		// which on a swell may be rising to meet it).
		if (bIsAirborne)
		{
			const float SinkCmS = SurfaceVerticalSpeed - Velocity.Z;
			const bool bReenteringWater = (SinkCmS >= 0.0f) && (PostMoveLocation.Z <= WaterHeight + WaterContactHeightCm);
			if (bReenteringWater && CurrentJumpAirtime > 0.05f)
			{
				bWasInWaterContact = true;

				// A skip off the surface is not a jump: carry on riding with nothing lost or scored.
				// (Only for the kite plucking the rider off: a pop is the rider's choice and always counts.)
				const float MinJumpApexCm = 50.0f;
				if (CurrentJumpApexHeight < MinJumpApexCm && bLiftedByKite)
				{
					BeginTouchdownAbsorb(SinkCmS);
					const bool bStillPlaning = Velocity.Size2D() >= PlaningThresholdCmS;
					CurrentBoardState = bStillPlaning ? EBoardState::Planing : EBoardState::Displacement;
					CurrentDragRegime = bStillPlaning ? EBoardDragRegime::Planing : EBoardDragRegime::Displacement;
					UpdateComponentVelocity();
					return;
				}

				LastJumpApexHeight = CurrentJumpApexHeight;
				LastJumpAirtime = CurrentJumpAirtime;
				LastJumpDistance = CurrentJumpDistance;
				// The landing's geometry: the board's tilt from the water and its yaw off the course (either
				// end: a switch landing is fine), and the rider's up. With the rider attitude the board is its
				// full orientation and the rider's own; without it, the root's orientation and an upright rider.
				const FQuat LandingBoardQuat = bAirBoardQuatInUse ? AirBoardQuat : UpdatedComponent->GetComponentQuat();
				const FQuat LandingBodyQuat = bAirBoardQuatInUse ? AirAttitudeBodyQuat : FQuat::Identity;
				const FVector LandingAngularVelocity = bAirBoardQuatInUse ? AirAttitudeAngularVelocity : FVector::ZeroVector;
				const FLandingGeometry LandingGeometry = LandingEvaluator::ComputeGeometry(LandingBoardQuat, WaterNormal, Velocity, LandingBodyQuat, LandingAngularVelocity);
				NoteJumpEnd(LandingGeometry.YawOffVelocityDeg);
				++JumpCount;
				if (CurrentJumpApexHeight > BestJumpHeight)
				{
					BestJumpHeight = CurrentJumpApexHeight;
				}
				BestJumpDistance = FMath::Max(BestJumpDistance, LastJumpDistance);

				// The landing's load (research 3.4): the sink v is taken out over the absorb distance s,
				// the legs and the board's immersion, longer for a crouch, at 1 + v^2 / (2 g s) g. It is
				// hot when the rider sinks fast or the kite is low (not holding them up); it is a crash if
				// the board is not lined up with its course or the load is more than the legs can take.
				LastLandingSinkMS = KiteUnits::CmToM(SinkCmS);
				LastLandingAbsorbCm = LandingAbsorbDistanceCm * (1.0f + FMath::Max(CrouchAbsorbBonus, 0.0f) * LoadAmount);
				LastLandingG = LandingGForSink(LastLandingSinkMS, LastLandingAbsorbCm);
				float KiteElevationDeg = 90.0f;
				if (const AActor* OwnerActor = GetOwner())
				{
					if (const UKiteComponent* KiteComp = OwnerActor->FindComponentByClass<UKiteComponent>())
					{
						KiteElevationDeg = KiteComp->GetElevationDeg();
					}
				}
				bLastLandingHot = LastLandingSinkMS > HotLandingSinkMS || KiteElevationDeg < HotLandingKiteElevationDeg;

				// The grade (docs/tricks.md 6.7): the geometry above, the sink, the g and the kite.
				FLandingInputs LandingInputs;
				LandingEvaluator::ApplyGeometry(LandingGeometry, LandingInputs);
				LandingInputs.SinkMS = LastLandingSinkMS;
				LandingInputs.LandingG = LastLandingG;
				LandingInputs.KiteElevationDeg = KiteElevationDeg;
				LandingInputs.bHotLanding = bLastLandingHot;
				LandingInputs.BackFoot = RiderBackFoot;
				FLandingThresholds Thresholds = LandingThresholds;
				Thresholds.CrashLandingG = CrashLandingG;
				Thresholds.HotLandingSinkMS = HotLandingSinkMS;
				Thresholds.HotLandingKiteElevationDeg = HotLandingKiteElevationDeg;
				LastLandingInputs = LandingInputs;
				LastLandingVerdict = LandingEvaluator::Evaluate(LandingInputs, Thresholds);

				// The rider is back on the water: the attitude's hold on the board ends here.
				bAirAttitudeActive = false;
				bAirBoardQuatInUse = false;

				const UEnum* GradeEnum = StaticEnum<ELandingGrade>();
				const UEnum* CauseEnum = StaticEnum<ELandingCause>();
				const FString GradeName = GradeEnum ? GradeEnum->GetNameStringByValue(static_cast<int64>(LastLandingVerdict.Grade)) : FString();
				const FString CauseName = CauseEnum ? CauseEnum->GetNameStringByValue(static_cast<int64>(LastLandingVerdict.Cause)) : FString();
				if (LastLandingVerdict.Grade != ELandingGrade::Crash)
				{
					// Landed: the absorber takes the sink out, and the rider keeps the grade's share of their speed.
					bLastLandingClean = true;
					bIsCrashing = false;
					Velocity.X *= LastLandingVerdict.SpeedRetention;
					Velocity.Y *= LastLandingVerdict.SpeedRetention;
					BeginTouchdownAbsorb(SinkCmS);

					CurrentBoardState = EBoardState::Landing;
					LandingStateTimer = 0.25f;

					OnBoardLandingVerdict.Broadcast(LastLandingVerdict);
					OnBoardLanding.Broadcast(LastLandingG);

					UE_LOG(LogKiteSurf, Log, TEXT("Landed %s%s%s: tilt %.1f deg, yaw %.1f deg, rider up %.2f. Apex: %.1f cm, Airtime: %.2f s, RetainedSpeed: %.1f kn (x%.2f), LandingG: %.2f (sink %.2f m/s over %.0f cm, kite %.0f deg up%s)"),
						*GradeName, LastLandingVerdict.Cause != ELandingCause::None ? TEXT(", ") : TEXT(""), LastLandingVerdict.Cause != ELandingCause::None ? *CauseName : TEXT(""),
						LandingInputs.TiltDeg, LandingInputs.YawOffVelocityDeg, LandingInputs.BodyUpDot, LastJumpApexHeight, LastJumpAirtime, KiteUnits::CmSToKnots(Velocity.Size2D()), LastLandingVerdict.SpeedRetention,
						LastLandingG, LastLandingSinkMS, LastLandingAbsorbCm, KiteElevationDeg, bLastLandingHot ? TEXT(", HOT") : TEXT(""));
				}
				else
				{
					// Crash landing: speed drops to 0 over 0.5 s, rider respawns upright after 1.5 s
					OnBoardLandingVerdict.Broadcast(LastLandingVerdict);
					TriggerCrash();

					UE_LOG(LogKiteSurf, Log, TEXT("Crash landing (%s)! Tilt %.1f deg, yaw %.1f deg, rider up %.2f, LandingG: %.2f (most %.1f; sink %.2f m/s over %.0f cm, kite %.0f deg up%s). Apex: %.1f cm, Airtime: %.2f s. Initiating crash sequence."),
						*CauseName, LandingInputs.TiltDeg, LandingInputs.YawOffVelocityDeg, LandingInputs.BodyUpDot, LastLandingG, CrashLandingG, LastLandingSinkMS, LastLandingAbsorbCm, KiteElevationDeg,
						bLastLandingHot ? TEXT(", HOT") : TEXT(""), LastJumpApexHeight, LastJumpAirtime);
				}
			}
		}

		UpdateComponentVelocity();
	}
}

float UBoardMovementComponent::UpwindOfBeamForHeading(float HeadingYawDeg, float PullYawDeg)
{
	// The beam reach of the pull is square to it on the side the heading is on: 90 deg either way from
	// the pull. Past it, away from the kite, is upwind of it.
	return FMath::Abs(FRotator::NormalizeAxis(HeadingYawDeg - PullYawDeg)) - 90.0f;
}

float UBoardMovementComponent::LimitTurnByHarness(float YawDeg, float TargetYawDeg, float PullYawDeg, float DeltaTime, bool& bOutHeldAgainst) const
{
	// The angle from the pull to the heading, on the side of the pull the board is on (0 pointing at the
	// kite, 90 on the beam reach, 180 straight away from it), before and after the turn; the turn is
	// followed from that side, so a turn through the kite's direction onto its other side is a turn
	// towards it, allowed, and one that would carry the heading round past straight away from the kite
	// is stopped at the limit on the way.
	const float FromPullDeg = FRotator::NormalizeAxis(YawDeg - PullYawDeg);
	const float Side = FromPullDeg >= 0.0f ? 1.0f : -1.0f;
	const float TurnDeg = FRotator::NormalizeAxis(TargetYawDeg - YawDeg);
	const float OffPullBefore = Side * FromPullDeg;
	const float OffPullWanted = OffPullBefore + Side * TurnDeg;
	const float OffPullLimit = 90.0f + FMath::Clamp(MaxUpwindHeadingDeg, 0.0f, 89.0f);
	// Inside the limit the turn may go up to it; outside it (the pull moved) the harness brings the
	// board back at its own rate, unless the turn itself comes back faster.
	const float OffPullAllowed = OffPullBefore <= OffPullLimit
		? OffPullLimit
		: FMath::Max(OffPullBefore - FMath::Max(HarnessYawRateDegPerS, 0.0f) * DeltaTime, OffPullLimit);
	bOutHeldAgainst = OffPullWanted > OffPullAllowed && Side * TurnDeg > 0.0f;
	if (OffPullWanted <= OffPullAllowed)
	{
		return TargetYawDeg;
	}
	return FRotator::NormalizeAxis(YawDeg + Side * (OffPullAllowed - OffPullBefore));
}

void UBoardMovementComponent::SetAirAttitude(const FQuat& BoardQuat, bool bActive, const FQuat& BodyQuat, const FVector& AngularVelocityRadS)
{
	bAirAttitudeActive = bActive;
	AirAttitudeBoardQuat = BoardQuat.GetNormalized();
	AirAttitudeBodyQuat = BodyQuat.GetNormalized();
	AirAttitudeAngularVelocity = AngularVelocityRadS;
}

FQuat UBoardMovementComponent::GetBoardWorldQuat() const
{
	if (bAirBoardQuatInUse && CurrentBoardState == EBoardState::Airborne)
	{
		return AirBoardQuat;
	}
	return UpdatedComponent ? UpdatedComponent->GetComponentQuat() : FQuat::Identity;
}

float UBoardMovementComponent::LandingGForSink(float SinkMS, float AbsorbDistanceCm)
{
	// A constant deceleration v^2 / (2 s) takes the sink out over s, on top of the rider's weight. The
	// trick recorder's helper is the same formula; one copy keeps the two cards on one number.
	return LandingMath::ComputeLandingG(KiteUnits::MToCm(SinkMS), AbsorbDistanceCm);
}

float UBoardMovementComponent::BeginTouchdownAbsorb(float SinkCmS)
{
	const float AbsorbCm = FMath::Max(LandingAbsorbDistanceCm * (1.0f + FMath::Max(CrouchAbsorbBonus, 0.0f) * LoadAmount), 1.0f);
	const float Sink = FMath::Max(SinkCmS, 0.0f);
	bAbsorbing = Sink > 0.0f;
	AbsorbDecelCmS2 = Sink * Sink / (2.0f * AbsorbCm);
	return AbsorbCm;
}

float UBoardMovementComponent::DecayWithLinearAndQuadraticDrag(float Speed, float LinearRatePerS, float QuadraticRatePerCm, float Seconds)
{
	// dv/dt = -a v - b v^2 has the closed form v(t) = a v0 / ((a + b v0) e^(a t) - b v0); with no
	// linear term it is v0 / (1 + b v0 t). Neither ever overshoots zero, whatever the step.
	if (Speed <= 0.0f || Seconds <= 0.0f)
	{
		return FMath::Max(Speed, 0.0f);
	}
	if (LinearRatePerS <= KINDA_SMALL_NUMBER)
	{
		return Speed / (1.0f + QuadraticRatePerCm * Speed * Seconds);
	}
	const float Growth = FMath::Exp(LinearRatePerS * Seconds);
	return LinearRatePerS * Speed / ((LinearRatePerS + QuadraticRatePerCm * Speed) * Growth - QuadraticRatePerCm * Speed);
}

FVector UBoardMovementComponent::UpdateHeelAndNormalSideForce(float DeltaTime, const FVector& PullN, const FVector& LevelRight, bool bOnWater)
{
	// The board carries the rider's weight less what the lines hold up (research 3.1); the pull
	// across its axis is what the heel has to hold.
	const float WeightN = FMath::Max(MassKg, 1.0f) * KiteUnits::GravityMS2;
	const float PullAcrossN = FVector::DotProduct(PullN, LevelRight);
	const float CarriedN = FMath::Max(WeightN - PullN.Z, 0.0f);
	LastStepDebug.PullAcrossN = PullAcrossN;
	LastStepDebug.CarriedN = CarriedN;

	// The heel the rider holds against the pull: the balance, tan(heel) = pull across / weight
	// carried, plus the load's extra on top, followed at HeelResponse. In the air there is nothing
	// to heel against and the board comes level.
	float TargetHeelDeg = 0.0f;
	if (bOnWater)
	{
		if (bAutoEdge)
		{
			TargetHeelDeg = FMath::RadiansToDegrees(FMath::Atan2(FMath::Abs(PullAcrossN), CarriedN));
		}
		// The load and the lean back against the harness both drive the edge in past the balance.
		TargetHeelDeg = FMath::Min(TargetHeelDeg + LoadAmount * LoadExtraHeelDeg + HarnessLeanAmount * HarnessLeanHeelDeg, MaxHeelDeg);
		// Against the pull; with no pull across the board the rider stays on the rail they are on.
		const float PullSide = FMath::Abs(PullAcrossN) > KINDA_SMALL_NUMBER ? FMath::Sign(PullAcrossN) : (BalanceHeelDeg < 0.0f ? -1.0f : 1.0f);
		TargetHeelDeg *= PullSide;
	}
	BalanceHeelDeg += (TargetHeelDeg - BalanceHeelDeg) * (1.0f - FMath::Exp(-FMath::Max(HeelResponse, 0.0f) * DeltaTime));

	// Carving, the rider leans into the turn the board is making (docs/physics/plan-2.md item 3c): a
	// turn to the right tilts the normal force to the board's right, which is negative heel. Towards
	// the kite that takes the edge off and the pull across turns the board; away from it the rail digs
	// in. The lean is the one the turn needs, tan(lean) = v w / g (research: 35 deg at 6.5 m/s and
	// 60 deg/s), w the rate the carve turned the board at in the last step, up to CarveHeelDeg. Until
	// plan-3 item 3 it was CarveHeelDeg times the input whatever the board did: on a board slowed
	// nearly to a stop that lean pushed it sideways far harder than the pull across, swung its
	// velocity round, and the carve, which leads the velocity, followed it through the wind. A turn
	// the harness stops has no lean into it; the lean back against the hook takes its place.
	const float TurnRadS = FMath::DegreesToRadians(CarveYawRateDegPerS);
	const float TurnLeanDeg = FMath::Min(FMath::RadiansToDegrees(FMath::Atan(KiteUnits::CmToM(Velocity.Size2D()) * FMath::Abs(TurnRadS) / KiteUnits::GravityMS2)), FMath::Max(CarveHeelDeg, 0.0f));
	const float CarveLeanDeg = bOnWater ? FMath::Sign(CarveYawRateDegPerS) * TurnLeanDeg : 0.0f;
	HeelDeg = FMath::Clamp(BalanceHeelDeg - CarveLeanDeg, -MaxHeelDeg, MaxHeelDeg);
	LastStepDebug.HeelDeg = HeelDeg;

	if (!bOnWater)
	{
		return FVector::ZeroVector;
	}
	// The water's normal force on the heeled board is N = carried / cos(heel); its sideways part,
	// N sin(heel), acts across the board against the pull. Only a rider standing on the board puts
	// their weight through it: sunk in the water (no planing speed) they float, and buoyancy is
	// vertical.
	const float OnBoard = FloatSubmersionCm > 0.0f ? FMath::Clamp(1.0f - CurrentFloatDepthCm / FloatSubmersionCm, 0.0f, 1.0f) : 1.0f;
	const float HeelRad = FMath::DegreesToRadians(FMath::Clamp(HeelDeg, -MaxHeelDeg, MaxHeelDeg));
	LastStepDebug.NormalForceN = CarriedN * OnBoard / FMath::Cos(HeelRad);
	const FVector NormalSideN = -LevelRight * (CarriedN * FMath::Tan(HeelRad) * OnBoard);
	LastStepDebug.NormalSideForceN = NormalSideN;
	return NormalSideN * KiteUnits::UnrealForcePerN;
}

void UBoardMovementComponent::ApplyWaterDragAndSideForce(float DeltaTime, float DragDecelCmS2, float DragRatePerS, float DragRatePerCm, const FVector& LevelForward)
{
	const FVector HorizontalBefore(Velocity.X, Velocity.Y, 0.0f);
	const float SpeedBefore = HorizontalBefore.Size();
	if (SpeedBefore <= KINDA_SMALL_NUMBER || DeltaTime <= 0.0f)
	{
		return;
	}
	const float EffectiveMass = FMath::Max(MassKg, 1.0f);

	// The hull's drag against the velocity over the water: the pressure drag, constant over the step,
	// takes its share of the speed (never past zero), then a + c v^2 decays the rest in closed form.
	const float SpeedAfterPressure = FMath::Max(SpeedBefore - FMath::Max(DragDecelCmS2, 0.0f) * DeltaTime, 0.0f);
	const float Speed = DecayWithLinearAndQuadraticDrag(SpeedAfterPressure, DragRatePerS, DragRatePerCm, DeltaTime);
	LastStepDebug.DragForceN = HorizontalBefore / SpeedBefore * (EffectiveMass * (Speed - SpeedBefore) / DeltaTime / KiteUnits::UnrealForcePerN);

	// Leeway: the angle from the board's axis (whichever end leads) to its velocity, positive to the
	// axis's right.
	const FVector Dragged = HorizontalBefore / SpeedBefore * Speed;
	const float Along = FVector::DotProduct(Dragged, LevelForward);
	const FVector Axis = Along >= 0.0f ? LevelForward : -LevelForward;
	const FVector AxisRight = FVector::CrossProduct(FVector::UpVector, Axis);
	const float AlongAbs = FMath::Abs(Along);
	const float Across = FVector::DotProduct(Dragged, AxisRight);
	LastStepDebug.LeewayDeg = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Dragged, FVector::CrossProduct(FVector::UpVector, LevelForward)), AlongAbs));

	// The fins and the immersed rail make side force from leeway, F = 0.5 rho_w v^2 A_lat C_L,beta
	// beta up to the leeway stall (research 2.3). Like any low aspect ratio plate the force is
	// normal to them, across the board's axis, so against the velocity it has a drag part F sin(beta).
	// More heel puts more rail in the water; weight on the tail digs it in, weight on the nose lifts it.
	const float RailScale = FMath::Pow(FMath::Max(TailWeightRailScale, 0.01f), -CurrentWeightShift);
	const float LateralAreaM2 = FMath::Max(FinAreaM2 + RailAreaM2 * FMath::Sin(FMath::DegreesToRadians(FMath::Abs(HeelDeg))) * RailScale, 0.0f);
	const float SpeedMS = Speed / KiteUnits::CmPerM;
	const float ForcePerRadN = 0.5f * KiteUnits::WaterDensityKgM3 * SpeedMS * SpeedMS * LateralAreaM2 * LateralLiftSlopePerRad;
	const float StallRad = FMath::DegreesToRadians(FMath::Max(LeewayStallDeg, 0.1f));
	// The force takes the sideways speed w out at dw/dt = -F / m, F = ForcePerRad * clamp(beta), beta
	// = atan(w / u): past the stall at the stall's constant rate, then (beta ~ w / u) as an exact
	// exponential decay, so it never overshoots.
	float AcrossAfterAbs = FMath::Abs(Across);
	if (ForcePerRadN > 0.0f)
	{
		const float StallAcross = AlongAbs * FMath::Tan(StallRad); // cm/s of sideways speed at the stall
		const float StallDecel = ForcePerRadN * StallRad * KiteUnits::UnrealForcePerN / EffectiveMass; // cm/s^2
		float SecondsLeft = DeltaTime;
		if (AcrossAfterAbs > StallAcross)
		{
			const float Seconds = FMath::Min((AcrossAfterAbs - StallAcross) / StallDecel, SecondsLeft);
			AcrossAfterAbs -= StallDecel * Seconds;
			SecondsLeft -= Seconds;
		}
		if (SecondsLeft > 0.0f)
		{
			const float RatePerS = AlongAbs > KINDA_SMALL_NUMBER ? ForcePerRadN * KiteUnits::UnrealForcePerN / (EffectiveMass * AlongAbs) : BIG_NUMBER;
			AcrossAfterAbs *= FMath::Exp(-RatePerS * SecondsLeft);
		}
	}
	const float AcrossAfter = FMath::Sign(Across) * AcrossAfterAbs;
	LastStepDebug.SideForceN = AxisRight * (EffectiveMass * (AcrossAfter - Across) / DeltaTime / KiteUnits::UnrealForcePerN);
	const FVector HorizontalAfter = Axis * AlongAbs + AxisRight * AcrossAfter;
	Velocity.X = HorizontalAfter.X;
	Velocity.Y = HorizontalAfter.Y;
}

FVector UBoardMovementComponent::ComputeAirDragForce(const FVector& Location, const FVector& InVelocity) const
{
	if (RiderDragAreaM2 <= 0.0f)
	{
		return FVector::ZeroVector;
	}
	// The wind the rider feels: at the height the kite samples it for them, at the board's own
	// simulation time. Without a wind component the air is still.
	FVector WindCmS = FVector::ZeroVector;
	if (const AActor* OwnerActor = GetOwner())
	{
		if (const UWindComponent* Wind = OwnerActor->FindComponentByClass<UWindComponent>())
		{
			const UKiteComponent* Kite = OwnerActor->FindComponentByClass<UKiteComponent>();
			const float WindHeightCm = Kite ? Kite->RiderWindHeightCm : 0.0f;
			WindCmS = Wind->GetWindAtTime(Location + FVector(0.0f, 0.0f, WindHeightCm), SimTimeSeconds);
		}
	}
	// In SI: the air moving past the rider, and the drag along it.
	const FVector ApparentWindMS = (WindCmS - InVelocity) / KiteUnits::CmPerM;
	const FVector DragN = 0.5f * KiteUnits::AirDensityKgM3 * RiderDragAreaM2 * ApparentWindMS.Size() * ApparentWindMS;
	return DragN * KiteUnits::UnrealForcePerN;
}

float UBoardMovementComponent::EffectiveMassForBuoyancy() const
{
	return FMath::Max(MassKg, 1.0f);
}

void UBoardMovementComponent::TriggerCrash(float CrashIntensity)
{
	bLastLandingClean = false;
	bIsCrashing = true;
	bAbsorbing = false;
	CrashTimer = 0.0f;
	CrashInitialVelocity = Velocity;
	Velocity.Z = 0.0f;
	CurrentBoardState = EBoardState::Landing;

	const float Intensity = CrashIntensity > 0.0f ? CrashIntensity : FMath::Clamp(Velocity.Size2D() / 1000.0f, 0.2f, 1.0f);
	OnBoardCrash.Broadcast(Intensity);
}

void UBoardMovementComponent::ResetToTack(float SpeedKnots)
{
	if (!UpdatedComponent)
	{
		return;
	}

	// 1. Determine previous tack direction
	FVector Forward2D = FVector::ForwardVector;
	if (CrashInitialVelocity.Size2D() > 10.0f)
	{
		Forward2D = CrashInitialVelocity.GetSafeNormal2D();
	}
	else if (Velocity.Size2D() > 10.0f)
	{
		Forward2D = Velocity.GetSafeNormal2D();
	}
	else
	{
		Forward2D = UpdatedComponent->GetForwardVector().GetSafeNormal2D();
		if (Forward2D.IsNearlyZero())
		{
			Forward2D = FVector(1.0f, 0.0f, 0.0f);
		}
	}

	// 2. Align board upright along tack heading
	FRotator TargetRot = Forward2D.Rotation();
	TargetRot.Pitch = 0.0f;
	TargetRot.Roll = 0.0f;
	UpdatedComponent->SetWorldRotation(TargetRot);

	// 3. Reset location to water surface
	const FVector Location = UpdatedComponent->GetComponentLocation();
	float WaterHeight = 0.0f;
	FVector WaterNormal = FVector::UpVector;
	SampleWaterSurface(Location, WaterHeight, WaterNormal);

	// On the surface if the reset speed planes, floating if it does not.
	CurrentFloatDepthCm = GetFloatDepthForSpeed(KiteUnits::KnotsToCmS(SpeedKnots));
	FVector RespawnLoc = Location;
	RespawnLoc.Z = WaterHeight - CurrentFloatDepthCm;
	UpdatedComponent->SetWorldLocation(RespawnLoc);

	// 4. Set velocity on tack (8 knots = 411.52 cm/s)
	const float SpeedCmS = KiteUnits::KnotsToCmS(SpeedKnots);
	Velocity = Forward2D * SpeedCmS;

	// 5. Clear crash and set rideable state
	bAbsorbing = false;
	bWasInWaterContact = true;
	HeelDeg = 0.0f;
	BalanceHeelDeg = 0.0f;
	HarnessLeanAmount = 0.0f;
	CarveYawRateDegPerS = 0.0f;
	bIsCrashing = false;
	CrashTimer = 0.0f;
	CrashInitialVelocity = FVector::ZeroVector;
	LandingStateTimer = 0.0f;

	if (SpeedCmS >= PlaningThresholdCmS)
	{
		CurrentBoardState = EBoardState::Planing;
		CurrentDragRegime = EBoardDragRegime::Planing;
	}
	else
	{
		CurrentBoardState = EBoardState::Displacement;
		CurrentDragRegime = EBoardDragRegime::Displacement;
	}

	// 6. Park the kite at 45 deg (10:30 or 1:30) on the side the board is riding towards, in the window the rider now feels
	if (AActor* OwnerActor = GetOwner())
	{
		if (UKiteComponent* Kite = OwnerActor->FindComponentByClass<UKiteComponent>())
		{
			const FVector CrosswindRight = FVector::CrossProduct(FVector::UpVector, Kite->GetDownwindDir());
			const float TackSide = FVector::DotProduct(Forward2D, CrosswindRight) >= 0.0f ? 1.0f : -1.0f;
			Kite->SetWindowPosition(45.0f * TackSide, 12.0f);
		}
	}

	// 7. Broadcast reset event
	++ResetCount;
	OnBoardReset.Broadcast();
}
