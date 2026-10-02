#include "BoardMovementComponent.h"
#include "KiteSurf.h"
#include "KiteComponent.h"
#include "KiteWaterSurface.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"
#include "KiteSurfUnits.h"

UBoardMovementComponent::UBoardMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Tunables (Spec defaults)
	MassKg = 85.0f;
	BoardLengthCm = 140.0f;
	BoardWidthCm = 42.0f;
	BuoyancyN = 1500.0f;
	PlaningThresholdCmS = 400.0f;
	DisplacementQuadraticDragKgPerCm = 0.1f;
	PlaningDragKgPerS = 8.0f;
	PlaningQuadraticDragKgPerCm = 0.03f; // mostly quadratic, so board speed scales with wind speed
	EdgeGripKgPerS = 2000.0f;
	MaxEdgeAngleDeg = 35.0f;
	MaxBoardSpeedCmS = KiteUnits::KnotsToCmS(35.0f);
	DisplacementDragKgPerS = 8.0f;
	EdgeDriveEfficiency = 0.35f;

	// Additional physics tuning
	BaseGripKgPerS = 500.0f; // fins and a neutral stance hold a course without edge input
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
	CurrentFloatDepthCm = 0.0f;
	TailWeightGripScale = 2.5f;
	TailWeightDrag = 0.35f;
	NoseWeightDragSaving = 0.15f;
	TailWeightHeelDeg = 12.0f;
	WeightShiftPitchDeg = 8.0f;
	AirWeightShiftPitchDeg = 30.0f;
	LiftoffWeightFactor = 1.5f; // a low, powered kite must not bounce the rider off the water
	EdgedLiftoffWeightBonus = 3.0f;
	AirSpinRate = 200.0f;
	TailWeightPopBonus = 0.5f;
	AutoHeelDeg = 12.0f;
	AutoHeelFullLoadN = 500.0f;

	// Jump tunables (Spec defaults)
	PopImpulseKgCmPerS = 21000.0f; // kg*cm/s: about 2.5 m/s from the legs alone; height comes from the kite
	EdgeReleaseSeconds = 0.22f;     // s: the release of the edge; after that the lines keep pulling as a force
	JumpMinSpeedKnots = 8.0f;   // 8 kn
	JumpMinEdgeInput = 0.4f;    // 0.4
	MaxJumpHeight = 4000.0f;    // 4000 cm = 40 m
	MaxLandingAngle = 30.0f;    // 30 deg
	CleanLandingSpeedRetention = 0.8f; // 80%
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

void UBoardMovementComponent::AddExternalForce(const FVector& Force)
{
	AccumulatedExternalForce += Force;
}

FString UBoardMovementComponent::JumpRejectReasonToString(EJumpRejectReason Reason)
{
	switch (Reason)
	{
	case EJumpRejectReason::NotPlaning:
		return TEXT("Not planing");
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
	// Jump only allowed while in Planing state
	if (CurrentBoardState != EBoardState::Planing)
	{
		const FString ReasonStr = JumpRejectReasonToString(EJumpRejectReason::NotPlaning);
		UE_LOG(LogKiteSurf, Log, TEXT("Jump rejected: %s"), *ReasonStr);
		return EJumpRejectReason::NotPlaning;
	}

	// Board speed must be at least JumpMinSpeedKnots
	const float SpeedKnots = KiteUnits::CmSToKnots(Velocity.Size2D());
	if (SpeedKnots < JumpMinSpeedKnots - KINDA_SMALL_NUMBER)
	{
		const FString ReasonStr = JumpRejectReasonToString(EJumpRejectReason::TooSlow);
		UE_LOG(LogKiteSurf, Log, TEXT("Jump rejected: %s"), *ReasonStr);
		return EJumpRejectReason::TooSlow;
	}

	// The edge must be loaded: either turning or with the weight back on the tail, by at least 0.4
	const float EdgeLoad = FMath::Max(FMath::Abs(CurrentEdgeInput), -CurrentWeightShift);
	if (EdgeLoad < JumpMinEdgeInput - KINDA_SMALL_NUMBER)
	{
		const FString ReasonStr = JumpRejectReasonToString(EJumpRejectReason::NotEdged);
		UE_LOG(LogKiteSurf, Log, TEXT("Jump rejected: %s"), *ReasonStr);
		return EJumpRejectReason::NotEdged;
	}

	float UpwardKiteForce = 0.0f;
	if (const AActor* OwnerActor = GetOwner())
	{
		if (const UKiteComponent* KiteComp = OwnerActor->FindComponentByClass<UKiteComponent>())
		{
			UpwardKiteForce = FMath::Max(0.0f, KiteComp->GetLineForce().Z);
		}
	}

	const float PopImpulse = PopImpulseKgCmPerS * (1.0f + TailWeightPopBonus * FMath::Max(-CurrentWeightShift, 0.0f));
	const float Impulse = PopImpulse + EdgeReleaseSeconds * UpwardKiteForce;
	const float EffectiveMass = FMath::Max(MassKg, 1.0f);
	const float VerticalDeltaV = Impulse / EffectiveMass;

	Velocity.Z = FMath::Max(Velocity.Z + VerticalDeltaV, VerticalDeltaV);

	BeginAirborne();

	UE_LOG(LogKiteSurf, Log, TEXT("Board Jump initiated: Speed=%.1f kn, Edge=%.2f, KiteLiftZ=%.1f, Impulse=%.1f, VZ=%.1f cm/s"),
		SpeedKnots, CurrentEdgeInput, UpwardKiteForce, Impulse, Velocity.Z);

	return EJumpRejectReason::None;
}

void UBoardMovementComponent::BeginAirborne()
{
	bLiftedByKite = false;
	CurrentBoardState = EBoardState::Airborne;
	CurrentJumpAirtime = 0.0f;
	CurrentJumpHeight = 0.0f;
	CurrentJumpApexHeight = 0.0f;
	LandingStateTimer = 0.0f;
}

void UBoardMovementComponent::SetBoardSize(EBoardSize InSize)
{
	BoardSize = KiteGear::BoardSizeFromIndex(static_cast<int32>(InSize));
	const FBoardSizeTraits Traits = KiteGear::GetTraits(BoardSize);
	// Reference 138 cm board: the constructor's values.
	PopImpulseKgCmPerS = 21000.0f * Traits.PopScale;
	PlaningThresholdCmS = 400.0f * Traits.PlaningSpeedScale;
	LowSpeedPivotMaxSpeedCmS = PlaningThresholdCmS;
	PlaningDragKgPerS = 8.0f * Traits.PlaningDragScale;
	PlaningQuadraticDragKgPerCm = 0.03f * Traits.PlaningDragScale;
	BaseGripKgPerS = 500.0f * Traits.GripScale;
	EdgeGripKgPerS = 2000.0f * Traits.GripScale;
	CarveTurnRate = 60.0f * Traits.TurnRateScale;
}

float UBoardMovementComponent::GetFloatDepthForSpeed(float SpeedCmS) const
{
	// Fully sunk below FloatUntilSpeedFraction of planing speed, on the surface at planing speed.
	const float Planing = FMath::SmoothStep(PlaningThresholdCmS * FloatUntilSpeedFraction, PlaningThresholdCmS, SpeedCmS);
	return FloatSubmersionCm * (1.0f - Planing);
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
	return FVector::DotProduct(Velocity, UpdatedComponent->GetForwardVector());
}

float UBoardMovementComponent::GetLateralSpeed() const
{
	if (!UpdatedComponent)
	{
		return Velocity.Y;
	}
	return FVector::DotProduct(Velocity, UpdatedComponent->GetRightVector());
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
	if (!ShouldSkipUpdate(DeltaTime) && UpdatedComponent)
	{
		const FVector Location = UpdatedComponent->GetComponentLocation();
		const FRotator Rotation = UpdatedComponent->GetComponentRotation();
		const FVector Forward = UpdatedComponent->GetForwardVector();
		const FVector Right = UpdatedComponent->GetRightVector();

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

		// 1. Water surface sampling
		float WaterHeight = 0.0f;
		FVector WaterNormal = FVector::UpVector;
		SampleWaterSurface(Location, WaterHeight, WaterNormal);

		bool bIsAirborne = (CurrentBoardState == EBoardState::Airborne);

		// Without planing speed the board does not carry the rider: they float, sunk to the chest,
		// and rise onto the surface as the board gets up to speed (the water start). A rider in
		// the air comes down onto the surface first and sinks from there.
		const float TargetFloatDepthCm = bIsAirborne ? 0.0f : GetFloatDepthForSpeed(Velocity.Size2D());
		CurrentFloatDepthCm = FMath::FInterpTo(CurrentFloatDepthCm, TargetFloatDepthCm, DeltaTime, FloatResponse);
		const float RideHeight = WaterHeight - CurrentFloatDepthCm;

		const float HeightAboveWater = Location.Z - WaterHeight;
		bool bHydrodynamicsDisabled = bIsAirborne && (HeightAboveWater > 10.0f);

		if (bIsAirborne)
		{
			CurrentJumpAirtime += DeltaTime;
			CurrentJumpHeight = FMath::Max(0.0f, HeightAboveWater);
			if (CurrentJumpHeight > CurrentJumpApexHeight)
			{
				CurrentJumpApexHeight = CurrentJumpHeight;
			}

			// Clamp apex at MaxJumpHeight (default 40 m = 4000 cm)
			if (Location.Z >= WaterHeight + MaxJumpHeight)
			{
				FVector ClampedLocation = Location;
				ClampedLocation.Z = WaterHeight + MaxJumpHeight;
				UpdatedComponent->SetWorldLocation(ClampedLocation);
				Velocity.Z = FMath::Min(Velocity.Z, 0.0f);
			}
		}

		// 2. Setup total forces
		FVector TotalForce = AccumulatedExternalForce;
		const float ExternalLateralForce = FVector::DotProduct(AccumulatedExternalForce, Right);
		const FVector ExternalForce2D(AccumulatedExternalForce.X, AccumulatedExternalForce.Y, 0.0f);
		AccumulatedExternalForce = FVector::ZeroVector;

		const float GravityZ = -KiteUnits::GravityCmS2; // the same g the kite uses, whatever the world settings say
		const float GravityForceZ = MassKg * GravityZ; // negative in kg*cm/s^2
		TotalForce.Z += GravityForceZ;

		// The kite lifts the rider off when it pulls up harder than they weigh: sending the kite
		// overhead or looping it does this without a pop.
		// A rider who is edging (carving, or with their weight back) leans against the lines with
		// the board dug in, and can hold a much harder pull down until they let the edge go.
		const float EdgeHold = FMath::Clamp(FMath::Max(FMath::Abs(CurrentEdgeInput), -CurrentWeightShift), 0.0f, 1.0f);
		const float LiftoffFactor = LiftoffWeightFactor + EdgedLiftoffWeightBonus * EdgeHold;
		if (!bIsAirborne && CurrentBoardState != EBoardState::Landing && TotalForce.Z > -GravityForceZ * (LiftoffFactor - 1.0f))
		{
			BeginAirborne();
			bLiftedByKite = true;
			bIsAirborne = true;
			Velocity.Z = FMath::Max(Velocity.Z, 0.0f);
			UE_LOG(LogKiteSurf, Log, TEXT("Board lifted off by the kite: upward force %.0f N against %.0f N of weight"), KiteUnits::UnrealForceToN(TotalForce.Z - GravityForceZ), KiteUnits::UnrealForceToN(-GravityForceZ));
		}

		// 3. Buoyancy & Vertical Dynamics (disabled while above water surface + 10 cm)
		// How far below its riding height the board is: that height is the surface when planing
		// and the floating depth when not.
		const float Submersion = RideHeight - Location.Z;
		if (!bHydrodynamicsDisabled && Submersion >= -15.0f)
		{
			// A damped spring about the ride height, set by its frequency and damping ratio.
			const float BuoyancyBalance = -GravityForceZ; // exactly balances gravity at rest
			const float OmegaRadS = 2.0f * PI * BuoyancyNaturalFrequencyHz;
			const float SpringForceZ = EffectiveMassForBuoyancy() * OmegaRadS * OmegaRadS * Submersion;
			const float DampingForceZ = -2.0f * BuoyancyDampingRatio * EffectiveMassForBuoyancy() * OmegaRadS * Velocity.Z;

			float BuoyancyForceZ = BuoyancyBalance + SpringForceZ + DampingForceZ;
			const float MaxBuoyancyForce = KiteUnits::NToUnrealForce(BuoyancyN);
			BuoyancyForceZ = FMath::Clamp(BuoyancyForceZ, 0.0f, MaxBuoyancyForce);

			TotalForce.Z += BuoyancyForceZ;
		}

		// 4. Horizontal Hydrodynamics (Displacement vs Planing, Edging, Lift)
		const float Speed2D = Velocity.Size2D();
		const float ForwardSpeed = FVector::DotProduct(Velocity, Forward);
		const float LateralSpeed = FVector::DotProduct(Velocity, Right);

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

		// Drag and grip are integrated exactly over the step (below), so they never overshoot
		// however long the step is. Here only the forces they give rise to are added: the planing
		// lift, and the drive a heeled rail makes out of the sideways grip force.
		const float EffectiveMass = FMath::Max(MassKg, 1.0f);
		float ForwardDragRatePerS = 0.0f;   // linear drag as a rate: a in dv/dt = -a v - b v^2
		float ForwardDragRatePerCm = 0.0f;  // quadratic drag as a rate: b
		float GripRatePerS = 0.0f;          // sideways grip as a rate
		if (!bHydrodynamicsDisabled)
		{
			if (!bPlaning)
			{
				// Displacement regime: quadratic + linear drag so depowering stops (< 2 kn) within 5s
				ForwardDragRatePerS = DisplacementDragKgPerS / EffectiveMass;
				ForwardDragRatePerCm = DisplacementQuadraticDragKgPerCm / EffectiveMass;
			}
			else
			{
				// Planing regime: linear drag + high-speed form/spray drag
				// A sunk tail drags more; weight forward flattens the board and frees it up.
				const float EdgeDragScale = 1.0f + TailWeightDrag * FMath::Max(-CurrentWeightShift, 0.0f) - NoseWeightDragSaving * FMath::Max(CurrentWeightShift, 0.0f);
				ForwardDragRatePerS = PlaningDragKgPerS * EdgeDragScale / EffectiveMass;
				ForwardDragRatePerCm = PlaningQuadraticDragKgPerCm * EdgeDragScale / EffectiveMass;

				// Hydrodynamic lift raising the board with surface contact falloff
				const float SurfaceContact = FMath::Clamp((Submersion + 10.0f) / 15.0f, 0.0f, 1.0f);
				const float PlaningLift = PlaningLiftKgPerS * (Speed2D - PlaningThresholdCmS) * SurfaceContact;
				TotalForce.Z += PlaningLift;
			}

			// Sideways grip. Weight on the tail digs the rail in and bites harder; on the nose the board lets go.
			const float EdgeFactor = FMath::Abs(CurrentEdgeInput);
			const float PressureGripScale = FMath::Pow(TailWeightGripScale, -CurrentWeightShift);
			GripRatePerS = (BaseGripKgPerS + EdgeGripKgPerS * EdgeFactor) * PressureGripScale / EffectiveMass;

			// Edge Drive: the rail lift of a heeled board turns the sideways grip force into forward
			// drive. The grip force is the sideways momentum the grip takes out of the board this step.
			if (ForwardSpeed > 50.0f && EdgeFactor > 0.01f && EdgeDriveEfficiency > 0.0f && FMath::Abs(LateralSpeed) > KINDA_SMALL_NUMBER)
			{
				const float LateralSpeedRemoved = FMath::Abs(LateralSpeed) * (1.0f - FMath::Exp(-GripRatePerS * DeltaTime));
				const float GripForce = EffectiveMass * LateralSpeedRemoved / FMath::Max(DeltaTime, KINDA_SMALL_NUMBER);
				TotalForce += Forward * (GripForce * EdgeFactor * EdgeDriveEfficiency);
			}
		}

		// 5. Velocity Integration: the forces, then the drag and grip as exact decays of the
		// board-frame velocity components, which is the same at any step length.
		Velocity += TotalForce / EffectiveMass * DeltaTime;
		if (!bHydrodynamicsDisabled)
		{
			const float ForwardNow = FVector::DotProduct(Velocity, Forward);
			const float LateralNow = FVector::DotProduct(Velocity, Right);
			const FVector Rest = Velocity - Forward * ForwardNow - Right * LateralNow;
			const float ForwardAfter = FMath::Sign(ForwardNow) * DecayWithLinearAndQuadraticDrag(FMath::Abs(ForwardNow), ForwardDragRatePerS, ForwardDragRatePerCm, DeltaTime);
			const float LateralAfter = LateralNow * FMath::Exp(-GripRatePerS * DeltaTime);
			Velocity = Rest + Forward * ForwardAfter + Right * LateralAfter;
		}

		// Velocity clamping at MaxBoardSpeed
		const float MaxSpeedCmS = GetMaxBoardSpeedCmS();
		if (Velocity.Size2D() > MaxSpeedCmS)
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
		if (bIsAirborne)
		{
			// In the air the carve input spins the board. Left alone, the rider brings it back in
			// line with the direction of travel (either way round) for the landing.
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
			const FVector LevelForward = FRotator(0.0f, Rotation.Yaw, 0.0f).Vector();
			const FVector LevelRight = FVector::CrossProduct(FVector::UpVector, LevelForward);
			const float SurfacePitch = FMath::RadiansToDegrees(FMath::Atan2(-FVector::DotProduct(WaterNormal, LevelForward), FVector::DotProduct(WaterNormal, FVector::UpVector)));
			const float SurfaceRoll = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(WaterNormal, LevelRight), FVector::DotProduct(WaterNormal, FVector::UpVector)));

			// Positive pitch is nose up: weight on the tail lifts the nose.
			TargetRotation.Pitch = FMath::Clamp(SurfacePitch - CurrentWeightShift * WeightShiftPitchDeg, -20.0f, 20.0f);
			// The rider leans away from the kite, so the rail on the kite's side lifts with the load.
			// Weight back heels further, weight forward takes the heel off.
			const float LoadSide = FMath::Clamp(ExternalLateralForce / KiteUnits::NToUnrealForce(AutoHeelFullLoadN), -1.0f, 1.0f);
			const float LoadHeelDeg = -LoadSide * FMath::Max(AutoHeelDeg - TailWeightHeelDeg * CurrentWeightShift, 0.0f);
			TargetRotation.Roll = FMath::Clamp(SurfaceRoll + LoadHeelDeg + SmoothedCarveInput * MaxEdgeAngleDeg, -MaxEdgeAngleDeg, MaxEdgeAngleDeg);

			// A twin-tip rides either way: once it is moving tail-first, the tail becomes the nose.
			if (ForwardSpeed < -SwitchStanceSpeedCmS && CurrentBoardState != EBoardState::Landing)
			{
				TargetRotation.Yaw = FRotator::NormalizeAxis(Rotation.Yaw + 180.0f);
				TargetRotation.Pitch = -TargetRotation.Pitch;
				TargetRotation.Roll = -TargetRotation.Roll;
			}
			// Edging changes board heading relative to velocity
			else if (FMath::Abs(ForwardSpeed) > 50.0f && FMath::Abs(SmoothedCarveInput) > 0.02f)
			{
				const float VelocityHeading = FMath::RadiansToDegrees(FMath::Atan2(Velocity.Y, Velocity.X));
				const float DesiredHeading = FRotator::NormalizeAxis(VelocityHeading + SmoothedCarveInput * MaxEdgeAngleDeg);
				const float DeltaYaw = FRotator::NormalizeAxis(DesiredHeading - Rotation.Yaw);
				const float MaxTurnStep = CarveTurnRate * FMath::Abs(SmoothedCarveInput) * FMath::Clamp(Speed2D / PlaningThresholdCmS, 0.5f, 1.0f) * DeltaTime;
				TargetRotation.Yaw = FRotator::NormalizeAxis(Rotation.Yaw + FMath::Clamp(DeltaYaw, -MaxTurnStep, MaxTurnStep));
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
				if (FMath::Abs(YawError) > 90.0f)
				{
					YawError = FRotator::NormalizeAxis(YawError + 180.0f); // either end of a twin-tip will do
				}
				// Full rate when stopped, easing to a fifth just below planing speed, so a slow drift
				// in the wrong direction still comes round.
				const float PivotScale = FMath::Clamp(1.0f - Speed2D / LowSpeedPivotMaxSpeedCmS, 0.2f, 1.0f);
				const float MaxStep = LowSpeedPivotRate * PivotScale * DeltaTime;
				TargetRotation.Yaw = FRotator::NormalizeAxis(Rotation.Yaw + FMath::Clamp(YawError, -MaxStep, MaxStep));
			}
		}

		// 7. Apply movement via SafeMoveUpdatedComponent
		const FVector MoveDelta = Velocity * DeltaTime;
		FHitResult Hit(1.0f);
		SafeMoveUpdatedComponent(MoveDelta, TargetRotation, true, Hit);

		if (Hit.IsValidBlockingHit())
		{
			SlideAlongSurface(MoveDelta, 1.0f - Hit.Time, Hit.Normal, Hit);
		}

		const FVector PostMoveLocation = UpdatedComponent->GetComponentLocation();

		// 8. Landing detection on re-entering water
		if (bIsAirborne)
		{
			const bool bReenteringWater = (Velocity.Z <= 0.0f) && (PostMoveLocation.Z <= WaterHeight + 10.0f);
			if (bReenteringWater && CurrentJumpAirtime > 0.05f)
			{
				const FVector Velocity2D = Velocity.GetSafeNormal2D();
				const FVector Forward2D = UpdatedComponent->GetForwardVector().GetSafeNormal2D();
				float LandingAngleDeg = 0.0f;
				if (!Velocity2D.IsNearlyZero() && !Forward2D.IsNearlyZero())
				{
					// A twin-tip lands either way round, so only the angle to the board's axis matters.
					const float Dot = FMath::Clamp(FMath::Abs(FVector::DotProduct(Forward2D, Velocity2D)), 0.0f, 1.0f);
					LandingAngleDeg = FMath::RadiansToDegrees(FMath::Acos(Dot));
				}

				// A skip off the surface is not a jump: carry on riding with nothing lost or scored.
				// (Only for the kite plucking the rider off: a pop is the rider's choice and always counts.)
				const float MinJumpApexCm = 50.0f;
				if (CurrentJumpApexHeight < MinJumpApexCm && bLiftedByKite)
				{
					Velocity.Z = 0.0f;
					const bool bStillPlaning = Velocity.Size2D() >= PlaningThresholdCmS;
					CurrentBoardState = bStillPlaning ? EBoardState::Planing : EBoardState::Displacement;
					CurrentDragRegime = bStillPlaning ? EBoardDragRegime::Planing : EBoardDragRegime::Displacement;
					UpdateComponentVelocity();
					return;
				}

				LastJumpApexHeight = CurrentJumpApexHeight;
				LastJumpAirtime = CurrentJumpAirtime;
				if (CurrentJumpApexHeight > BestJumpHeight)
				{
					BestJumpHeight = CurrentJumpApexHeight;
				}

				if (LandingAngleDeg <= MaxLandingAngle)
				{
					// Clean landing: keep 80% of speed
					bLastLandingClean = true;
					bIsCrashing = false;
					const float VerticalSpeed = FMath::Abs(Velocity.Z);
					Velocity.X *= CleanLandingSpeedRetention;
					Velocity.Y *= CleanLandingSpeedRetention;
					Velocity.Z = 0.0f;

					FVector LandedLoc = PostMoveLocation;
					LandedLoc.Z = WaterHeight;
					UpdatedComponent->SetWorldLocation(LandedLoc);

					CurrentBoardState = EBoardState::Landing;
					LandingStateTimer = 0.25f;

					const float LandingG = FMath::Clamp(VerticalSpeed / KiteUnits::GravityCmS2, 1.0f, 10.0f);
					OnBoardLanding.Broadcast(LandingG);

					UE_LOG(LogKiteSurf, Log, TEXT("Clean landing! Angle: %.1f deg <= %.1f deg. Apex: %.1f cm, Airtime: %.2f s, RetainedSpeed: %.1f kn, LandingG: %.2f"),
						LandingAngleDeg, MaxLandingAngle, LastJumpApexHeight, LastJumpAirtime, KiteUnits::CmSToKnots(Velocity.Size2D()), LandingG);
				}
				else
				{
					// Crash landing: speed drops to 0 over 0.5 s, rider respawns upright after 1.5 s
					FVector LandedLoc = PostMoveLocation;
					LandedLoc.Z = WaterHeight;
					UpdatedComponent->SetWorldLocation(LandedLoc);

					TriggerCrash();

					UE_LOG(LogKiteSurf, Log, TEXT("Crash landing! Angle: %.1f deg > %.1f deg. Apex: %.1f cm, Airtime: %.2f s. Initiating crash sequence."),
						LandingAngleDeg, MaxLandingAngle, LastJumpApexHeight, LastJumpAirtime);
				}
			}
		}

		// Water contact constraint: keep within +/- 20 cm of water height when NOT airborne
		if (CurrentBoardState != EBoardState::Airborne)
		{
			const FVector NewLocation = UpdatedComponent->GetComponentLocation();
			const float CurrentSubmersion = RideHeight - NewLocation.Z;
			if (FMath::Abs(CurrentSubmersion) > 20.0f)
			{
				FVector ClampedLocation = NewLocation;
				ClampedLocation.Z = FMath::Clamp(NewLocation.Z, RideHeight - 19.99f, RideHeight + 19.99f);
				UpdatedComponent->SetWorldLocation(ClampedLocation);
				Velocity.Z = 0.0f;
			}

			if (CurrentBoardState == EBoardState::Planing)
			{
				ensureAlwaysMsgf(FMath::Abs(RideHeight - UpdatedComponent->GetComponentLocation().Z) <= 20.0f + KINDA_SMALL_NUMBER,
					TEXT("BoardMovement: Planing pawn out of water contact: Submersion = %.2f cm (expected within +/- 20 cm)"),
					RideHeight - UpdatedComponent->GetComponentLocation().Z);
			}
		}

		UpdateComponentVelocity();
	}
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

float UBoardMovementComponent::EffectiveMassForBuoyancy() const
{
	return FMath::Max(MassKg, 1.0f);
}

void UBoardMovementComponent::TriggerCrash(float CrashIntensity)
{
	bLastLandingClean = false;
	bIsCrashing = true;
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
