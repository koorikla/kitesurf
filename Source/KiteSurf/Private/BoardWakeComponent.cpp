#include "BoardWakeComponent.h"
#include "KiteSurfUnits.h"
#include "BoardMovementComponent.h"
#include "KiteComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"

namespace
{
	const float SprayGravityCmS2 = -KiteUnits::GravityCmS2;
	const float FullSpraySpeedCmS = KiteUnits::KnotsToCmS(20.0f);
	// Basic shape meshes are 100 cm across.
	const float BasicShapeSizeCm = 100.0f;
	// Foam sits just above the surface so it does not z-fight with the water.
	const float FoamLiftCm = 4.0f;
}

UBoardWakeComponent::UBoardWakeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;

	MinWakeSpeedCmS = 250.0f;
	FoamSpacingCm = 60.0f;
	FoamLifetime = 5.0f;
	FoamStartSizeCm = 70.0f;
	FoamEndSizeCm = 320.0f;
	FoamOpacity = 0.45f;
	MaxFoamPatches = 200;
	SprayRatePerSec = 320.0f;
	MaxSprayDrops = 320;

	DistanceSinceFoamCm = 0.0f;
	SprayDebt = 0.0f;
	Random.Initialize(1337);
}

void UBoardWakeComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* Owner = GetOwner())
	{
		BoardMovement = Owner->FindComponentByClass<UBoardMovementComponent>();
		if (BoardMovement)
		{
			AddTickPrerequisiteComponent(BoardMovement);
			BoardMovement->OnBoardLanding.AddDynamic(this, &UBoardWakeComponent::HandleBoardLanding);
			BoardMovement->OnBoardCrash.AddDynamic(this, &UBoardWakeComponent::HandleBoardCrash);
		}
	}

	if (AActor* Owner = GetOwner())
	{
		if (UKiteComponent* Kite = Owner->FindComponentByClass<UKiteComponent>())
		{
			Kite->OnKiteCrashed.AddDynamic(this, &UBoardWakeComponent::HandleKiteCrashed);
		}
	}

	FoamInstances = CreateInstances(TEXT("WakeFoam"), TEXT("/Engine/BasicShapes/Plane"), TEXT("/Game/Materials/M_WaterFoam"), MaxFoamPatches);
	SprayInstances = CreateInstances(TEXT("WakeSpray"), TEXT("/Engine/BasicShapes/Sphere"), TEXT("/Game/Materials/M_WaterSpray"), MaxSprayDrops);
}

UInstancedStaticMeshComponent* UBoardWakeComponent::CreateInstances(const TCHAR* Name, const TCHAR* MeshPath, const TCHAR* MaterialPath, int32 Count)
{
	AActor* Owner = GetOwner();
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath);
	if (!Owner || !Mesh || Count <= 0)
	{
		return nullptr;
	}

	// Not attached to the pawn: the wake stays where it was laid down, in world space.
	UInstancedStaticMeshComponent* Instances = NewObject<UInstancedStaticMeshComponent>(Owner, Name);
	Instances->SetStaticMesh(Mesh);
	if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, MaterialPath))
	{
		Instances->SetMaterial(0, Material);
	}
	Instances->SetMobility(EComponentMobility::Movable);
	Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Instances->SetCastShadow(false);
	Instances->SetNumCustomDataFloats(1); // opacity
	Instances->RegisterComponent();

	TArray<FTransform> Hidden;
	Hidden.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), Count);
	Instances->AddInstances(Hidden, false, true);
	return Instances;
}

void UBoardWakeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	Simulate(DeltaTime);
	UpdateInstances();
}

void UBoardWakeComponent::Simulate(float DeltaTime)
{
	if (!BoardMovement && GetOwner())
	{
		BoardMovement = GetOwner()->FindComponentByClass<UBoardMovementComponent>();
	}

	// Age out what is already on the water or in the air.
	for (int32 Index = FoamPatches.Num() - 1; Index >= 0; --Index)
	{
		FoamPatches[Index].Age += DeltaTime;
		if (FoamPatches[Index].Age >= FoamLifetime)
		{
			FoamPatches.RemoveAtSwap(Index, EAllowShrinking::No);
		}
	}
	for (int32 Index = SprayDrops.Num() - 1; Index >= 0; --Index)
	{
		FSprayDrop& Drop = SprayDrops[Index];
		Drop.Age += DeltaTime;
		Drop.Velocity.Z += SprayGravityCmS2 * DeltaTime;
		Drop.Position += Drop.Velocity * DeltaTime;
		if (Drop.Age >= Drop.Lifetime)
		{
			SprayDrops.RemoveAtSwap(Index, EAllowShrinking::No);
		}
	}

	const AActor* Owner = GetOwner();
	if (!Owner || !BoardMovement || DeltaTime <= 0.0f)
	{
		return;
	}

	const FVector Velocity = BoardMovement->Velocity;
	const float Speed = Velocity.Size2D();
	const bool bOnWater = BoardMovement->GetBoardState() != EBoardState::Airborne && !BoardMovement->IsCrashing();
	if (!bOnWater || Speed < MinWakeSpeedCmS)
	{
		DistanceSinceFoamCm = 0.0f;
		SprayDebt = 0.0f;
		return;
	}

	const FVector BoardLocation = Owner->GetActorLocation();
	const FVector Forward = Owner->GetActorForwardVector().GetSafeNormal2D();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
	const float SpeedFactor = FMath::Clamp(Speed / FullSpraySpeedCmS, 0.0f, 1.5f);

	// Foam: one patch at the tail for every FoamSpacingCm travelled.
	DistanceSinceFoamCm += Speed * DeltaTime;
	while (DistanceSinceFoamCm >= FoamSpacingCm)
	{
		DistanceSinceFoamCm -= FoamSpacingCm;
		if (FoamPatches.Num() >= MaxFoamPatches)
		{
			continue;
		}
		FFoamPatch& Patch = FoamPatches.AddDefaulted_GetRef();
		const FVector Tail = BoardLocation - Forward * (BoardMovement->BoardLengthCm * 0.5f + DistanceSinceFoamCm)
			+ Right * Random.FRandRange(-12.0f, 12.0f);
		Patch.Position = FVector2D(Tail);
		Patch.YawDeg = Random.FRandRange(0.0f, 360.0f);
		Patch.Age = 0.0f;
		Patch.Strength = FMath::Clamp(SpeedFactor, 0.35f, 1.0f);
	}

	// Spray: thrown up and out from the rail the board is pushing against, more of it when the
	// board is loaded (sliding sideways or held on an edge).
	const float LateralSpeed = FVector::DotProduct(Velocity, Right);
	const float Load = FMath::Clamp(FMath::Abs(LateralSpeed) / 200.0f + FMath::Abs(BoardMovement->GetEdgeInput()), 0.0f, 1.0f);
	const float SpraySide = LateralSpeed >= 0.0f ? 1.0f : -1.0f;
	SprayDebt += SprayRatePerSec * SpeedFactor * (0.3f + 0.7f * Load) * DeltaTime;
	while (SprayDebt >= 1.0f)
	{
		SprayDebt -= 1.0f;
		const FVector Start = BoardLocation
			+ Forward * Random.FRandRange(-0.5f, 0.1f) * BoardMovement->BoardLengthCm
			+ Right * SpraySide * BoardMovement->BoardWidthCm * 0.5f;
		const FVector Kick = Right * SpraySide * Random.FRandRange(60.0f, 260.0f)
			+ Forward * Random.FRandRange(-0.25f, 0.35f) * Speed
			+ FVector::UpVector * Random.FRandRange(150.0f, 420.0f) * FMath::Clamp(SpeedFactor, 0.5f, 1.2f);
		AddSprayDrop(Start, Kick);
	}
}

void UBoardWakeComponent::AddSprayDrop(const FVector& Position, const FVector& Velocity)
{
	if (SprayDrops.Num() >= MaxSprayDrops)
	{
		return;
	}
	FSprayDrop& Drop = SprayDrops.AddDefaulted_GetRef();
	Drop.Position = Position;
	Drop.Velocity = Velocity;
	Drop.Age = 0.0f;
	Drop.Lifetime = Random.FRandRange(0.35f, 0.75f);
	Drop.SizeCm = Random.FRandRange(2.0f, 5.0f);
}

void UBoardWakeComponent::EmitSplash(float Intensity)
{
	if (const AActor* Owner = GetOwner())
	{
		EmitSplashAt(Owner->GetActorLocation(), Intensity);
	}
}

void UBoardWakeComponent::EmitSplashAt(const FVector& Location, float Intensity)
{
	const int32 NumDrops = FMath::Clamp(FMath::RoundToInt(40.0f * Intensity), 10, 120);
	for (int32 Index = 0; Index < NumDrops; ++Index)
	{
		const float AngleRad = Random.FRandRange(0.0f, 2.0f * UE_PI);
		const FVector Outward(FMath::Cos(AngleRad), FMath::Sin(AngleRad), 0.0f);
		AddSprayDrop(Location + Outward * Random.FRandRange(10.0f, 60.0f),
			Outward * Random.FRandRange(120.0f, 380.0f) + FVector::UpVector * Random.FRandRange(250.0f, 600.0f) * FMath::Clamp(Intensity, 0.5f, 1.5f));
	}
}

void UBoardWakeComponent::HandleKiteCrashed(FVector WaterLocation)
{
	EmitSplashAt(FVector(WaterLocation.X, WaterLocation.Y, 0.0f), 2.5f);
}

void UBoardWakeComponent::HandleBoardLanding(float LandingG)
{
	EmitSplash(FMath::Clamp(LandingG / 2.0f, 0.5f, 2.0f));
}

void UBoardWakeComponent::HandleBoardCrash(float Intensity)
{
	EmitSplash(2.0f * FMath::Clamp(Intensity, 0.5f, 1.5f));
}

void UBoardWakeComponent::UpdateInstances()
{
	const FTransform HiddenTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);

	if (FoamInstances)
	{
		const int32 Count = FoamInstances->GetInstanceCount();
		TArray<FTransform> Transforms;
		Transforms.Init(HiddenTransform, Count);
		for (int32 Index = 0; Index < FMath::Min(Count, FoamPatches.Num()); ++Index)
		{
			const FFoamPatch& Patch = FoamPatches[Index];
			const float Life = FMath::Clamp(Patch.Age / FoamLifetime, 0.0f, 1.0f);
			const float SizeCm = FMath::Lerp(FoamStartSizeCm, FoamEndSizeCm, FMath::Sqrt(Life));

			// Ride the swell: the patch takes the water height where it lies.
			float WaterHeight = 0.0f;
			FVector WaterNormal = FVector::UpVector;
			if (BoardMovement)
			{
				BoardMovement->SampleWaterSurface(FVector(Patch.Position, 0.0f), WaterHeight, WaterNormal);
			}

			Transforms[Index] = FTransform(
				FRotator(0.0f, Patch.YawDeg, 0.0f),
				FVector(Patch.Position, WaterHeight + FoamLiftCm),
				FVector(SizeCm / BasicShapeSizeCm));
			FoamInstances->SetCustomDataValue(Index, 0, FoamOpacity * Patch.Strength * FMath::Square(1.0f - Life), false);
		}
		FoamInstances->BatchUpdateInstancesTransforms(0, Transforms, true, true, false);
	}

	if (SprayInstances)
	{
		const int32 Count = SprayInstances->GetInstanceCount();
		TArray<FTransform> Transforms;
		Transforms.Init(HiddenTransform, Count);
		for (int32 Index = 0; Index < FMath::Min(Count, SprayDrops.Num()); ++Index)
		{
			const FSprayDrop& Drop = SprayDrops[Index];
			const float Life = FMath::Clamp(Drop.Age / Drop.Lifetime, 0.0f, 1.0f);
			Transforms[Index] = FTransform(FQuat::Identity, Drop.Position, FVector(Drop.SizeCm * (1.0f + Life) / BasicShapeSizeCm));
			SprayInstances->SetCustomDataValue(Index, 0, 0.6f * (1.0f - Life), false);
		}
		SprayInstances->BatchUpdateInstancesTransforms(0, Transforms, true, true, false);
	}
}
