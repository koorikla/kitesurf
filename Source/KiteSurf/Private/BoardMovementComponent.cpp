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
	PlaningDragCoef = 40.0f;
	EdgeGripCoef = 400.0f;
	MaxEdgeAngleDeg = 45.0f;

	// Additional physics tuning
	BaseLateralDragCoef = 30.0f;
	BuoyancySpringStiffness = 3000.0f;
	BuoyancyDamping = 800.0f;
	PlaningLiftCoef = 50.0f;
	CarveTurnRate = 45.0f;

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
		if (Submersion >= -10.0f)
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
				// Displacement regime: drag proportional to v^2
				ForwardDragMagnitude = DisplacementDragCoef * (ForwardSpeed * ForwardSpeed) * ForwardSign;
			}
			else
			{
				// Planing regime: drag proportional to v
				ForwardDragMagnitude = PlaningDragCoef * ForwardSpeed;

				// Hydrodynamic lift raising the board
				const float PlaningLift = PlaningLiftCoef * (Speed2D - PlaningThresholdCmS);
				TotalForce.Z += PlaningLift;
			}
		}
		TotalForce -= Forward * ForwardDragMagnitude;

		// Lateral Resistance / Edging
		if (FMath::Abs(LateralSpeed) > KINDA_SMALL_NUMBER)
		{
			const float LateralResistanceCoef = BaseLateralDragCoef + EdgeGripCoef * FMath::Abs(CurrentEdgeInput);
			const float LateralDragMagnitude = LateralResistanceCoef * LateralSpeed;
			TotalForce -= Right * LateralDragMagnitude;
		}

		// 5. Velocity Integration
		const float EffectiveMass = FMath::Max(MassKg, 1.0f);
		const FVector Acceleration = TotalForce / EffectiveMass;
		Velocity += Acceleration * DeltaTime;

		// 6. Orientation Alignment: keep roll/pitch aligned with water normal + edge angle
		FRotator TargetRotation = Rotation;
		const float SurfacePitch = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(WaterNormal, Forward), FVector::DotProduct(WaterNormal, FVector::UpVector)));
		const float SurfaceRoll = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(WaterNormal, Right), FVector::DotProduct(WaterNormal, FVector::UpVector)));

		TargetRotation.Pitch = SurfacePitch;
		TargetRotation.Roll = SurfaceRoll + CurrentEdgeInput * MaxEdgeAngleDeg;

		// Carve yaw with edge when moving forward
		if (FMath::Abs(ForwardSpeed) > 50.0f && FMath::Abs(CurrentEdgeInput) > 0.02f)
		{
			TargetRotation.Yaw += CurrentEdgeInput * CarveTurnRate * (ForwardSpeed / PlaningThresholdCmS) * DeltaTime;
		}

		// 7. Apply movement via SafeMoveUpdatedComponent
		const FVector MoveDelta = Velocity * DeltaTime;
		FHitResult Hit(1.0f);
		SafeMoveUpdatedComponent(MoveDelta, TargetRotation, true, Hit);

		if (Hit.IsValidBlockingHit())
		{
			SlideAlongSurface(MoveDelta, 1.0f - Hit.Time, Hit.Normal, Hit);
		}

		UpdateComponentVelocity();
	}
}
