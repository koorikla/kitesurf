#include "BoardMovementComponent.h"
#include "KiteSurf.h"
#include "KiteComponent.h"
#include "KiteWaterSurface.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"

UBoardMovementComponent::UBoardMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Tunables (Spec defaults)
	MassKg = 85.0f;
	BoardLengthCm = 140.0f;
	BoardWidthCm = 42.0f;
	BuoyancyN = 1500.0f;
	PlaningThresholdCmS = 400.0f;
	DisplacementDragCoef = 0.1f;
	PlaningDragCoef = 45.0f;
	EdgeGripCoef = 400.0f;
	MaxEdgeAngleDeg = 35.0f;
	MaxBoardSpeed = 35.0f * 51.44f; // 1800.4 cm/s (35 kn)
	LinearDisplacementDragCoef = 8.0f;
	EdgeDriveEfficiency = 0.35f;

	// Additional physics tuning
	BaseLateralDragCoef = 30.0f;
	BuoyancySpringStiffness = 3000.0f;
	BuoyancyDamping = 800.0f;
	PlaningLiftCoef = 50.0f;
	CarveTurnRate = 120.0f;

	// Jump tunables (Spec defaults)
	BaseJumpImpulse = 35000.0f; // kg*cm/s
	KiteLiftFactor = 0.8f;      // s
	JumpMinSpeedKnots = 8.0f;   // 8 kn
	JumpMinEdgeInput = 0.4f;    // 0.4
	MaxJumpHeight = 1200.0f;    // 1200 cm = 12 m
	MaxLandingAngle = 30.0f;    // 30 deg
	CleanLandingSpeedRetention = 0.8f; // 80%
	CrashDecelDuration = 0.5f;
	CrashRespawnDelay = 1.5f;
	MaxSendWindowSeconds = 0.35f;
	EdgeLoadPopScalar = 1.0f;
	SendSweepRateScalar = 1.0f;
	EffectiveGravityCmS2 = 220.0f; // Calibrated 2.20 m/s^2 effective downward acceleration for kite float

	CurrentDragRegime = EBoardDragRegime::Displacement;
	CurrentBoardState = EBoardState::Displacement;
	CurrentEdgeInput = 0.0f;
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

	// Board speed >= 8 kn (1 kn = 51.44 cm/s)
	const float SpeedKnots = Velocity.Size2D() / 51.44f;
	if (SpeedKnots < JumpMinSpeedKnots - KINDA_SMALL_NUMBER)
	{
		const FString ReasonStr = JumpRejectReasonToString(EJumpRejectReason::TooSlow);
		UE_LOG(LogKiteSurf, Log, TEXT("Jump rejected: %s"), *ReasonStr);
		return EJumpRejectReason::TooSlow;
	}

	// Edge input >= 0.4
	if (FMath::Abs(CurrentEdgeInput) < JumpMinEdgeInput - KINDA_SMALL_NUMBER)
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

	const float Impulse = BaseJumpImpulse + KiteLiftFactor * UpwardKiteForce;
	const float EffectiveMass = FMath::Max(MassKg, 1.0f);
	const float VerticalDeltaV = Impulse / EffectiveMass;

	Velocity.Z = FMath::Max(Velocity.Z + VerticalDeltaV, VerticalDeltaV);

	CurrentBoardState = EBoardState::Airborne;
	CurrentJumpAirtime = 0.0f;
	CurrentJumpHeight = 0.0f;
	CurrentJumpApexHeight = 0.0f;
	LandingStateTimer = 0.0f;

	UE_LOG(LogKiteSurf, Log, TEXT("Board Jump initiated: Speed=%.1f kn, Edge=%.2f, KiteLiftZ=%.1f, Impulse=%.1f, VZ=%.1f cm/s"),
		SpeedKnots, CurrentEdgeInput, UpwardKiteForce, Impulse, Velocity.Z);

	return EJumpRejectReason::None;
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
				// Rider respawns upright after 1.5 s at crash position (no camera cut)
				FRotator CurrentRot = UpdatedComponent->GetComponentRotation();
				CurrentRot.Pitch = 0.0f;
				CurrentRot.Roll = 0.0f;
				UpdatedComponent->SetWorldRotation(CurrentRot);

				float WaterHeight = 0.0f;
				FVector WaterNormal = FVector::UpVector;
				SampleWaterSurface(Location, WaterHeight, WaterNormal);

				FVector RespawnLoc = Location;
				RespawnLoc.Z = WaterHeight;
				UpdatedComponent->SetWorldLocation(RespawnLoc);

				Velocity = FVector::ZeroVector;
				bIsCrashing = false;
				CrashTimer = 0.0f;
				CurrentBoardState = EBoardState::Displacement;
				CurrentDragRegime = EBoardDragRegime::Displacement;
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

		const bool bIsAirborne = (CurrentBoardState == EBoardState::Airborne);
		const float HeightAboveWater = Location.Z - WaterHeight;
		const bool bHydrodynamicsDisabled = bIsAirborne && (HeightAboveWater > 10.0f);

		if (bIsAirborne)
		{
			CurrentJumpAirtime += DeltaTime;
			CurrentJumpHeight = FMath::Max(0.0f, HeightAboveWater);
			if (CurrentJumpHeight > CurrentJumpApexHeight)
			{
				CurrentJumpApexHeight = CurrentJumpHeight;
			}

			// Clamp apex at MaxJumpHeight (default 12 m = 1200 cm)
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
		AccumulatedExternalForce = FVector::ZeroVector;

		const float GravityZ = GetGravityZ(); // -980 cm/s^2
		const float GravityForceZ = MassKg * GravityZ; // negative in kg*cm/s^2
		TotalForce.Z += GravityForceZ;

		// 3. Buoyancy & Vertical Dynamics (disabled while above water surface + 10 cm)
		const float Submersion = WaterHeight - Location.Z;
		if (!bHydrodynamicsDisabled && Submersion >= -15.0f)
		{
			const float BuoyancyBalance = -GravityForceZ; // exactly balances gravity at rest
			const float SpringForceZ = BuoyancySpringStiffness * Submersion;
			const float DampingForceZ = -BuoyancyDamping * Velocity.Z;

			float BuoyancyForceZ = BuoyancyBalance + SpringForceZ + DampingForceZ;
			const float MaxBuoyancyForce = BuoyancyN * 100.0f;
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

		if (!bHydrodynamicsDisabled)
		{
			// Forward Drag
			float ForwardDragMagnitude = 0.0f;
			if (FMath::Abs(ForwardSpeed) > KINDA_SMALL_NUMBER)
			{
				const float ForwardSign = FMath::Sign(ForwardSpeed);
				if (!bPlaning)
				{
					// Displacement regime: quadratic + linear drag so depowering stops (< 2 kn) within 5s
					ForwardDragMagnitude = (DisplacementDragCoef * (ForwardSpeed * ForwardSpeed) + LinearDisplacementDragCoef * FMath::Abs(ForwardSpeed)) * ForwardSign;
				}
				else
				{
					// Planing regime: linear drag + high-speed form/spray drag
					ForwardDragMagnitude = (PlaningDragCoef * ForwardSpeed + 0.015f * (ForwardSpeed * ForwardSpeed)) * ForwardSign;

					// Hydrodynamic lift raising the board with surface contact falloff
					const float SurfaceContact = FMath::Clamp((Submersion + 10.0f) / 15.0f, 0.0f, 1.0f);
					const float PlaningLift = PlaningLiftCoef * (Speed2D - PlaningThresholdCmS) * SurfaceContact;
					TotalForce.Z += PlaningLift;
				}
			}
			TotalForce -= Forward * ForwardDragMagnitude;

			// Lateral Resistance / Edging
			if (FMath::Abs(LateralSpeed) > KINDA_SMALL_NUMBER)
			{
				const float EdgeFactor = FMath::Abs(CurrentEdgeInput);
				const float LateralResistanceCoef = BaseLateralDragCoef + EdgeGripCoef * EdgeFactor;
				const float LateralDragMagnitude = LateralResistanceCoef * LateralSpeed;
				TotalForce -= Right * LateralDragMagnitude;

				// Edge Drive: hydrodynamic rail lift converts lateral resistance into forward drive when moving forward
				if (ForwardSpeed > 50.0f && EdgeFactor > 0.01f && EdgeDriveEfficiency > 0.0f)
				{
					const float ForwardDrive = FMath::Abs(LateralDragMagnitude) * EdgeFactor * EdgeDriveEfficiency;
					TotalForce += Forward * ForwardDrive;
				}
			}
		}

		// 5. Velocity Integration
		const float EffectiveMass = FMath::Max(MassKg, 1.0f);
		const FVector Acceleration = TotalForce / EffectiveMass;
		Velocity += Acceleration * DeltaTime;

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
		FRotator TargetRotation = Rotation;
		if (!bIsAirborne)
		{
			const float SurfacePitch = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(WaterNormal, Forward), FVector::DotProduct(WaterNormal, FVector::UpVector)));
			const float SurfaceRoll = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(WaterNormal, Right), FVector::DotProduct(WaterNormal, FVector::UpVector)));

			TargetRotation.Pitch = FMath::Clamp(SurfacePitch, -15.0f, 15.0f);
			TargetRotation.Roll = FMath::Clamp(SurfaceRoll + CurrentEdgeInput * MaxEdgeAngleDeg, -MaxEdgeAngleDeg, MaxEdgeAngleDeg);

			// Edging changes board heading relative to velocity
			if (FMath::Abs(ForwardSpeed) > 50.0f && FMath::Abs(CurrentEdgeInput) > 0.02f)
			{
				const float VelocityHeading = FMath::RadiansToDegrees(FMath::Atan2(Velocity.Y, Velocity.X));
				const float DesiredHeading = FRotator::NormalizeAxis(VelocityHeading + CurrentEdgeInput * MaxEdgeAngleDeg);
				const float DeltaYaw = FRotator::NormalizeAxis(DesiredHeading - Rotation.Yaw);
				const float MaxTurnStep = CarveTurnRate * FMath::Clamp(Speed2D / PlaningThresholdCmS, 0.5f, 2.0f) * DeltaTime;
				TargetRotation.Yaw = FRotator::NormalizeAxis(Rotation.Yaw + FMath::Clamp(DeltaYaw, -MaxTurnStep, MaxTurnStep));
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
					const float Dot = FMath::Clamp(FVector::DotProduct(Forward2D, Velocity2D), -1.0f, 1.0f);
					LandingAngleDeg = FMath::RadiansToDegrees(FMath::Acos(Dot));
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
					Velocity.X *= CleanLandingSpeedRetention;
					Velocity.Y *= CleanLandingSpeedRetention;
					Velocity.Z = 0.0f;

					FVector LandedLoc = PostMoveLocation;
					LandedLoc.Z = WaterHeight;
					UpdatedComponent->SetWorldLocation(LandedLoc);

					CurrentBoardState = EBoardState::Landing;
					LandingStateTimer = 0.25f;

					UE_LOG(LogKiteSurf, Log, TEXT("Clean landing! Angle: %.1f deg <= %.1f deg. Apex: %.1f cm, Airtime: %.2f s, RetainedSpeed: %.1f kn"),
						LandingAngleDeg, MaxLandingAngle, LastJumpApexHeight, LastJumpAirtime, Velocity.Size2D() / 51.44f);
				}
				else
				{
					// Crash landing: speed drops to 0 over 0.5 s, rider respawns upright after 1.5 s
					bLastLandingClean = false;
					bIsCrashing = true;
					CrashTimer = 0.0f;
					CrashInitialVelocity = Velocity;
					Velocity.Z = 0.0f;

					FVector LandedLoc = PostMoveLocation;
					LandedLoc.Z = WaterHeight;
					UpdatedComponent->SetWorldLocation(LandedLoc);

					CurrentBoardState = EBoardState::Landing;

					UE_LOG(LogKiteSurf, Log, TEXT("Crash landing! Angle: %.1f deg > %.1f deg. Apex: %.1f cm, Airtime: %.2f s. Initiating crash sequence."),
						LandingAngleDeg, MaxLandingAngle, LastJumpApexHeight, LastJumpAirtime);
				}
			}
		}

		// Water contact constraint: keep within +/- 20 cm of water height when NOT airborne
		if (CurrentBoardState != EBoardState::Airborne)
		{
			const FVector NewLocation = UpdatedComponent->GetComponentLocation();
			const float CurrentSubmersion = WaterHeight - NewLocation.Z;
			if (FMath::Abs(CurrentSubmersion) > 20.0f)
			{
				FVector ClampedLocation = NewLocation;
				ClampedLocation.Z = FMath::Clamp(NewLocation.Z, WaterHeight - 19.99f, WaterHeight + 19.99f);
				UpdatedComponent->SetWorldLocation(ClampedLocation);
				Velocity.Z = 0.0f;
			}

			if (CurrentBoardState == EBoardState::Planing)
			{
				ensureAlwaysMsgf(FMath::Abs(WaterHeight - UpdatedComponent->GetComponentLocation().Z) <= 20.0f + KINDA_SMALL_NUMBER,
					TEXT("BoardMovement: Planing pawn out of water contact: Submersion = %.2f cm (expected within +/- 20 cm)"),
					WaterHeight - UpdatedComponent->GetComponentLocation().Z);
			}
		}

		UpdateComponentVelocity();
	}
}
