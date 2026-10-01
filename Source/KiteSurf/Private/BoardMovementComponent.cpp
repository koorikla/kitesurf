#include "BoardMovementComponent.h"
#include "KiteSurf.h"
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
	CarveTurnRate = 90.0f;

	CurrentDragRegime = EBoardDragRegime::Displacement;
	CurrentEdgeInput = 0.0f;
	AccumulatedExternalForce = FVector::ZeroVector;
}

void UBoardMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	float DummyHeight = 0.0f;
	FVector DummyNormal = FVector::UpVector;
	SampleWaterSurface(GetActorLocation(), DummyHeight, DummyNormal);
}

void UBoardMovementComponent::SampleWaterSurface(const FVector& Location, float& OutWaterHeight, FVector& OutWaterNormal) const
{
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
		FVector SurfaceLocation = FVector::ZeroVector;
		FVector SurfaceNormal = FVector::UpVector;
		FVector SurfaceVelocity = FVector::ZeroVector;
		float Depth = 0.0f;

		if (CachedWaterBodyComponent->GetWaterSurfaceInfoAtLocation(Location, SurfaceLocation, SurfaceNormal, SurfaceVelocity, Depth, false))
		{
			OutWaterHeight = SurfaceLocation.Z;
			OutWaterNormal = SurfaceNormal;
		}
	}
}

void UBoardMovementComponent::AddExternalForce(const FVector& Force)
{
	AccumulatedExternalForce += Force;
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

		// 1. Water surface sampling
		float WaterHeight = 0.0f;
		FVector WaterNormal = FVector::UpVector;
		SampleWaterSurface(Location, WaterHeight, WaterNormal);

		// 2. Setup total forces
		FVector TotalForce = AccumulatedExternalForce;
		AccumulatedExternalForce = FVector::ZeroVector;

		const float GravityZ = GetGravityZ(); // -980 cm/s^2
		const float GravityForceZ = MassKg * GravityZ; // negative in kg*cm/s^2
		TotalForce.Z += GravityForceZ;

		// 3. Buoyancy & Vertical Dynamics
		const float Submersion = WaterHeight - Location.Z;
		if (Submersion >= -15.0f)
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
		if (bPlaning && CurrentDragRegime != EBoardDragRegime::Planing)
		{
			CurrentDragRegime = EBoardDragRegime::Planing;
			UE_LOG(LogKiteSurf, Log, TEXT("Board transitioned to Planing drag regime (Speed: %.1f cm/s >= Threshold: %.1f cm/s)"), Speed2D, PlaningThresholdCmS);
		}
		else if (!bPlaning && CurrentDragRegime != EBoardDragRegime::Displacement)
		{
			CurrentDragRegime = EBoardDragRegime::Displacement;
			UE_LOG(LogKiteSurf, Log, TEXT("Board transitioned to Displacement drag regime (Speed: %.1f cm/s < Threshold: %.1f cm/s)"), Speed2D, PlaningThresholdCmS);
		}

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

				// Hydrodynamic lift raising the board with surface contact falloff so board does not launch into air
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

		// 7. Apply movement via SafeMoveUpdatedComponent
		const FVector MoveDelta = Velocity * DeltaTime;
		FHitResult Hit(1.0f);
		SafeMoveUpdatedComponent(MoveDelta, TargetRotation, true, Hit);

		if (Hit.IsValidBlockingHit())
		{
			SlideAlongSurface(MoveDelta, 1.0f - Hit.Time, Hit.Normal, Hit);
		}

		// Water contact constraint: keep within +/- 20 cm of water height when planing
		const FVector NewLocation = UpdatedComponent->GetComponentLocation();
		const float CurrentSubmersion = WaterHeight - NewLocation.Z;
		if (FMath::Abs(CurrentSubmersion) > 20.0f)
		{
			FVector ClampedLocation = NewLocation;
			ClampedLocation.Z = FMath::Clamp(NewLocation.Z, WaterHeight - 20.0f, WaterHeight + 20.0f);
			UpdatedComponent->SetWorldLocation(ClampedLocation);
			Velocity.Z = 0.0f;
		}

		if (bPlaning)
		{
			ensureAlwaysMsgf(FMath::Abs(WaterHeight - UpdatedComponent->GetComponentLocation().Z) <= 20.0f,
				TEXT("BoardMovement: Planing pawn out of water contact: Submersion = %.2f cm (expected within +/- 20 cm)"),
				WaterHeight - UpdatedComponent->GetComponentLocation().Z);
		}

		UpdateComponentVelocity();
	}
}
