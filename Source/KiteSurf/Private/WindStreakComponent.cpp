#include "WindStreakComponent.h"
#include "BoardMovementComponent.h"
#include "WindComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

UWindStreakComponent::UWindStreakComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	StreakCount = 170;
	FieldRadiusCm = 16000.0f; // 160 m
	StreakLengthCm = 2000.0f;
	StreakWidthCm = 45.0f;
	DriftFraction = 0.35f;
	MaxOpacity = 0.65f;
	MinWindKnots = 4.0f;
	FullStrengthWindKnots = 18.0f;
	HeightAboveWaterCm = 8.0f;
	Random.Initialize(7919);
}

void UWindStreakComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane"));
	if (!Owner || !Plane)
	{
		return;
	}

	// Not attached to the pawn: the streaks lie on the water in world space. The foam material
	// fades to nothing at the edge of the plane, so a long thin plane is a soft-ended streak.
	Instances = NewObject<UInstancedStaticMeshComponent>(Owner, TEXT("WindStreakInstances"));
	Instances->SetStaticMesh(Plane);
	if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_WaterFoam")))
	{
		Instances->SetMaterial(0, Material);
	}
	Instances->SetMobility(EComponentMobility::Movable);
	Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Instances->SetCastShadow(false);
	Instances->SetNumCustomDataFloats(1); // opacity
	Instances->RegisterComponent();

	TArray<FTransform> Hidden;
	Hidden.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), FMath::Max(StreakCount, 1));
	Instances->AddInstances(Hidden, false, true);
}

float UWindStreakComponent::GetStrengthForWind(float WindKnots) const
{
	return FMath::Clamp((WindKnots - MinWindKnots) / FMath::Max(FullStrengthWindKnots - MinWindKnots, 1.0f), 0.0f, 1.0f);
}

void UWindStreakComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	Simulate(DeltaTime);
	UpdateInstances();
}

void UWindStreakComponent::Simulate(float DeltaTime)
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	if (!Wind)
	{
		Wind = Owner->FindComponentByClass<UWindComponent>();
	}
	if (!BoardMovement)
	{
		BoardMovement = Owner->FindComponentByClass<UBoardMovementComponent>();
	}

	const FVector OwnerLocation = Owner->GetActorLocation();
	const FVector2D Centre(OwnerLocation.X, OwnerLocation.Y);
	const FVector WindVector = Wind ? Wind->GetWindAt(OwnerLocation) : FVector::ZeroVector;
	const FVector2D Wind2D(WindVector.X, WindVector.Y);
	Strength = GetStrengthForWind(Wind2D.Size() / 51.44f);
	if (Wind2D.SizeSquared() > 1.0f)
	{
		StreakYawDeg = FMath::RadiansToDegrees(FMath::Atan2(Wind2D.Y, Wind2D.X));
	}

	auto RandomPointInField = [this, &Centre]()
	{
		// Even over the disc's area.
		const float Radius = FieldRadiusCm * FMath::Sqrt(Random.FRand());
		return Centre + FVector2D(Radius, 0.0f).GetRotated(Random.FRandRange(0.0f, 360.0f));
	};

	if (!bSeeded || Streaks.Num() != StreakCount)
	{
		Streaks.SetNum(FMath::Max(StreakCount, 0));
		for (FWindStreak& Streak : Streaks)
		{
			Streak.Position = RandomPointInField();
			Streak.LengthScale = Random.FRandRange(0.6f, 1.5f);
		}
		bSeeded = true;
	}

	const FVector2D Drift = Wind2D * DriftFraction * DeltaTime;
	for (FWindStreak& Streak : Streaks)
	{
		Streak.Position += Drift;

		// A streak that has left the field (drifted out downwind, or left behind by the rider)
		// comes back in on the far side, where it is faded right down, so nothing pops.
		FVector2D Offset = Streak.Position - Centre;
		if (Offset.SizeSquared() > FMath::Square(FieldRadiusCm))
		{
			if (Offset.SizeSquared() > FMath::Square(2.0f * FieldRadiusCm))
			{
				Streak.Position = RandomPointInField(); // the rider was moved a long way: start again
			}
			else
			{
				Streak.Position = Centre - Offset.GetSafeNormal() * FieldRadiusCm * 0.98f;
			}
			Streak.LengthScale = Random.FRandRange(0.6f, 1.5f);
			Offset = Streak.Position - Centre;
		}

		// Full in the middle of the field, nothing at its edge.
		const float EdgeFade = 1.0f - FMath::Clamp(Offset.Size() / FMath::Max(FieldRadiusCm, 1.0f), 0.0f, 1.0f);
		Streak.Opacity = FMath::SmoothStep(0.0f, 0.35f, EdgeFade) * Strength;
	}
}

void UWindStreakComponent::UpdateInstances()
{
	if (!Instances)
	{
		return;
	}

	const AActor* Owner = GetOwner();
	const float FallbackHeight = Owner ? Owner->GetActorLocation().Z : 0.0f;
	const int32 Count = FMath::Min(Streaks.Num(), Instances->GetInstanceCount());
	const FQuat Rotation = FRotator(0.0f, StreakYawDeg, 0.0f).Quaternion();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FWindStreak& Streak = Streaks[Index];
		float WaterHeight = FallbackHeight;
		if (BoardMovement)
		{
			FVector Normal = FVector::UpVector;
			BoardMovement->SampleWaterSurface(FVector(Streak.Position.X, Streak.Position.Y, 0.0f), WaterHeight, Normal);
		}
		// The engine's plane is 100 cm square.
		const FVector Scale(StreakLengthCm * Streak.LengthScale / 100.0f, StreakWidthCm / 100.0f, 1.0f);
		const FTransform Transform(Rotation, FVector(Streak.Position.X, Streak.Position.Y, WaterHeight + HeightAboveWaterCm), Scale);
		Instances->UpdateInstanceTransform(Index, Transform, true, false, true);
		Instances->SetCustomDataValue(Index, 0, Streak.Opacity * MaxOpacity, false);
	}
	Instances->MarkRenderStateDirty();
}
