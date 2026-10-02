#include "KiteSurfSpot.h"
#include "BoardMovementComponent.h"
#include "KiteRiderPawn.h"
#include "KiteSurf.h"
#include "KiteSurfHUD.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// The ellipsoids in scripts/editor/generate_mesh_objs.py (ISLAND_RADII, SANDBAR_RADII and
	// their centre depths). Change them together.
	const FVector IslandRadii(2000.0f, 1500.0f, 350.0f);
	const float IslandCentreZ = -80.0f;
	const FVector SandbarRadii(3000.0f, 400.0f, 90.0f);
	const float SandbarCentreZ = -40.0f;

	const FVector& RadiiOf(ESpotObstacleType Type)
	{
		return Type == ESpotObstacleType::Island ? IslandRadii : SandbarRadii;
	}

	float CentreZOf(ESpotObstacleType Type)
	{
		return Type == ESpotObstacleType::Island ? IslandCentreZ : SandbarCentreZ;
	}
}

float FSpotObstacle::GetSandHeightCm(const FVector2D& WorldXY) const
{
	const FVector2D Local = (WorldXY - Centre).GetRotated(-YawDeg);
	const FVector& Radii = RadiiOf(Type);
	const float Inside = 1.0f - FMath::Square(Local.X / Radii.X) - FMath::Square(Local.Y / Radii.Y);
	if (Inside <= 0.0f)
	{
		return CentreZOf(Type);
	}
	return CentreZOf(Type) + Radii.Z * FMath::Sqrt(Inside);
}

FVector FSpotObstacle::GetVisibleExtent() const
{
	const FVector& Radii = RadiiOf(Type);
	const float CentreZ = CentreZOf(Type);
	// Where the ellipsoid crosses the water.
	const float Shrink = FMath::Sqrt(FMath::Max(1.0f - FMath::Square(-CentreZ / Radii.Z), 0.0f));
	return FVector(Radii.X * Shrink, Radii.Y * Shrink, CentreZ + Radii.Z);
}

AKiteSurfSpot::AKiteSurfSpot()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	ClearStartRadiusCm = 15000.0f; // 150 m
	SharkCount = 4;
	SharkPatrolRadiusCm = 4000.0f;
	SharkPatrolSpeedCmS = 350.0f;
	SharkHuntSpeedCmS = 650.0f;
	SharkNoticeRadiusCm = 9000.0f;
	SharkBiteRadiusCm = 260.0f;
	SharkClearHeightCm = 80.0f;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> IslandFinder(TEXT("/Game/Meshes/SM_Island"));
	IslandMesh = IslandFinder.Succeeded() ? IslandFinder.Object : nullptr;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SandbarFinder(TEXT("/Game/Meshes/SM_Sandbar"));
	SandbarMesh = SandbarFinder.Succeeded() ? SandbarFinder.Object : nullptr;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SharkFinder(TEXT("/Game/Meshes/SM_Shark"));
	SharkMesh = SharkFinder.Succeeded() ? SharkFinder.Object : nullptr;
}

void AKiteSurfSpot::Setup(AKiteRiderPawn* InRider, const FVector& Origin, const FVector& DownwindDir, bool bIslands, bool bSandbars, bool bSharks)
{
	Rider = InRider;
	SpotOrigin = FVector(Origin.X, Origin.Y, 0.0f);
	SpotDownwind = DownwindDir.GetSafeNormal2D();
	if (SpotDownwind.IsNearlyZero())
	{
		SpotDownwind = FVector::ForwardVector;
	}
	bHasLayout = true;
	LastEvent.Reset();
	SetFeatures(bIslands, bSandbars, bSharks);
}

void AKiteSurfSpot::SetFeatures(bool bIslands, bool bSandbars, bool bSharks)
{
	bIslandsOn = bIslands;
	bSandbarsOn = bSandbars;
	bSharksOn = bSharks;

	Obstacles.Reset();
	Sharks.Reset();
	if (!bHasLayout)
	{
		RebuildVisuals();
		return;
	}

	// Laid out in wind coordinates: Across is the way a ride starts out (to the right looking
	// downwind), Down is downwind. A rider reaches back and forth across the wind and drifts
	// down it, so the sandbars lie across their path as things to jump, and the islands are
	// further off as places to ride round.
	const FVector2D Origin(SpotOrigin.X, SpotOrigin.Y);
	const FVector2D Down(SpotDownwind.X, SpotDownwind.Y);
	const FVector2D Across(-Down.Y, Down.X);
	const float DownYawDeg = FMath::RadiansToDegrees(FMath::Atan2(Down.Y, Down.X));
	auto Place = [&](ESpotObstacleType Type, float AcrossM, float DownM, float YawOffDownwindDeg)
	{
		FSpotObstacle Obstacle;
		Obstacle.Type = Type;
		Obstacle.Centre = Origin + Across * AcrossM * 100.0f + Down * DownM * 100.0f;
		Obstacle.YawDeg = DownYawDeg + YawOffDownwindDeg;
		Obstacles.Add(Obstacle);
	};
	if (bSandbarsOn)
	{
		// Long axis along the wind: a wall across the rider's reach, 8 m thick to clear.
		Place(ESpotObstacleType::Sandbar, 260.0f, 30.0f, 0.0f);
		Place(ESpotObstacleType::Sandbar, -320.0f, 70.0f, 0.0f);
		Place(ESpotObstacleType::Sandbar, 620.0f, 140.0f, 25.0f);
		Place(ESpotObstacleType::Sandbar, -700.0f, 220.0f, -20.0f);
	}
	if (bIslandsOn)
	{
		Place(ESpotObstacleType::Island, 950.0f, 60.0f, 70.0f);
		Place(ESpotObstacleType::Island, -1000.0f, 180.0f, 110.0f);
		Place(ESpotObstacleType::Island, 150.0f, 800.0f, 90.0f);
	}

	if (bSharksOn)
	{
		const FVector2D PatrolOffsetsM[] = { FVector2D(140.0f, 110.0f), FVector2D(-220.0f, 60.0f), FVector2D(430.0f, -70.0f), FVector2D(-480.0f, 260.0f), FVector2D(60.0f, 420.0f), FVector2D(780.0f, 260.0f) };
		for (int32 Index = 0; Index < FMath::Min(SharkCount, static_cast<int32>(UE_ARRAY_COUNT(PatrolOffsetsM))); ++Index)
		{
			FSpotShark Shark;
			Shark.PatrolCentre = Origin + Across * PatrolOffsetsM[Index].X * 100.0f + Down * PatrolOffsetsM[Index].Y * 100.0f;
			Shark.PatrolAngleDeg = 90.0f * Index;
			Shark.Position = Shark.PatrolCentre + FVector2D(SharkPatrolRadiusCm, 0.0f).GetRotated(Shark.PatrolAngleDeg);
			Shark.HeadingDeg = Shark.PatrolAngleDeg + 90.0f;
			Sharks.Add(Shark);
		}
	}

	RebuildVisuals();
}

void AKiteSurfSpot::RebuildVisuals()
{
	auto Sync = [this](TArray<TObjectPtr<UStaticMeshComponent>>& Meshes, int32 Wanted)
	{
		while (Meshes.Num() > Wanted)
		{
			if (UStaticMeshComponent* Extra = Meshes.Pop())
			{
				Extra->DestroyComponent();
			}
		}
		while (Meshes.Num() < Wanted)
		{
			UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this);
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh->SetupAttachment(RootComponent);
			if (GetWorld())
			{
				Mesh->RegisterComponent();
			}
			Meshes.Add(Mesh);
		}
	};

	Sync(ObstacleMeshes, Obstacles.Num());
	for (int32 Index = 0; Index < Obstacles.Num(); ++Index)
	{
		const FSpotObstacle& Obstacle = Obstacles[Index];
		UStaticMeshComponent* Mesh = ObstacleMeshes[Index];
		Mesh->SetStaticMesh(Obstacle.Type == ESpotObstacleType::Island ? IslandMesh : SandbarMesh);
		Mesh->SetWorldLocationAndRotation(FVector(Obstacle.Centre.X, Obstacle.Centre.Y, SpotOrigin.Z), FRotator(0.0f, Obstacle.YawDeg, 0.0f));
	}

	Sync(SharkMeshes, Sharks.Num());
	for (UStaticMeshComponent* Mesh : SharkMeshes)
	{
		Mesh->SetStaticMesh(SharkMesh);
	}
	UpdateSharkVisuals();
}

void AKiteSurfSpot::UpdateSharkVisuals()
{
	for (int32 Index = 0; Index < Sharks.Num() && Index < SharkMeshes.Num(); ++Index)
	{
		const FSpotShark& Shark = Sharks[Index];
		SharkMeshes[Index]->SetWorldLocationAndRotation(FVector(Shark.Position.X, Shark.Position.Y, SpotOrigin.Z), FRotator(0.0f, Shark.HeadingDeg, 0.0f));
	}
}

float AKiteSurfSpot::GetSandHeightCm(const FVector& WorldLocation) const
{
	float Highest = -1000.0f;
	const FVector2D XY(WorldLocation.X, WorldLocation.Y);
	for (const FSpotObstacle& Obstacle : Obstacles)
	{
		Highest = FMath::Max(Highest, Obstacle.GetSandHeightCm(XY));
	}
	return Highest;
}

FVector AKiteSurfSpot::FindWaterNear(const FVector& From, const FVector& TowardsDir) const
{
	// Back the way the rider came, a metre at a time, until there is water with room to spare.
	const FVector Step = TowardsDir.GetSafeNormal2D() * 100.0f;
	FVector Candidate = From;
	const float ClearMarginCm = 300.0f;
	for (int32 Tries = 0; Tries < 120 && !Step.IsNearlyZero(); ++Tries)
	{
		Candidate += Step;
		if (GetSandHeightCm(Candidate) <= 0.0f && GetSandHeightCm(Candidate + Step.GetSafeNormal() * ClearMarginCm) <= 0.0f)
		{
			return Candidate + Step.GetSafeNormal() * ClearMarginCm;
		}
	}
	return Candidate;
}

void AKiteSurfSpot::CrashRider(const FString& Event, const FVector& PutBackAt)
{
	LastEvent = Event;
	UE_LOG(LogKiteSurf, Log, TEXT("Spot: %s"), *Event);
	if (!Rider)
	{
		return;
	}
	// Its own sound first: the crash that follows then keeps its splash to itself.
	Rider->PlayRideSound(Event == TEXT("Shark!") ? ERideSound::Shark : ERideSound::Aground);
	if (UBoardMovementComponent* Board = Rider->GetBoardMovement())
	{
		Board->TriggerCrash(1.0f);
	}
	Rider->SetActorLocation(FVector(PutBackAt.X, PutBackAt.Y, Rider->GetActorLocation().Z));
	if (const APlayerController* PC = Cast<APlayerController>(Rider->GetController()))
	{
		if (AKiteSurfHUD* HUD = Cast<AKiteSurfHUD>(PC->GetHUD()))
		{
			HUD->ShowNotice(Event);
		}
	}
}

void AKiteSurfSpot::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	StepSpot(DeltaTime);
}

void AKiteSurfSpot::StepSpot(float DeltaTime)
{
	const UBoardMovementComponent* Board = Rider ? Rider->GetBoardMovement() : nullptr;
	const FVector RiderLocation = Rider ? Rider->GetActorLocation() : FVector::ZeroVector;
	const FVector2D RiderXY(RiderLocation.X, RiderLocation.Y);
	const bool bRiderDown = Board && (Board->IsFloating() || Board->IsCrashing());
	const bool bRiderCrashing = Board && Board->IsCrashing();
	// The rider's feet are at the actor's origin; the water is at the spot's origin height.
	const float RiderHeightCm = RiderLocation.Z - SpotOrigin.Z;

	// Sharks: round their patrol circle, unless there is a rider down in the water close by.
	for (FSpotShark& Shark : Sharks)
	{
		Shark.CalmSeconds = FMath::Max(Shark.CalmSeconds - DeltaTime, 0.0f);
		const FVector2D ToRider = RiderXY - Shark.Position;
		Shark.bHunting = Board && bRiderDown && Shark.CalmSeconds <= 0.0f && ToRider.SizeSquared() <= FMath::Square(SharkNoticeRadiusCm);

		FVector2D Target;
		float Speed;
		if (Shark.bHunting)
		{
			Target = RiderXY;
			Speed = SharkHuntSpeedCmS;
		}
		else
		{
			Shark.PatrolAngleDeg += FMath::RadiansToDegrees(SharkPatrolSpeedCmS / FMath::Max(SharkPatrolRadiusCm, 1.0f)) * DeltaTime;
			Target = Shark.PatrolCentre + FVector2D(SharkPatrolRadiusCm, 0.0f).GetRotated(Shark.PatrolAngleDeg);
			Speed = SharkPatrolSpeedCmS * 1.5f; // enough to get back onto the circle after a hunt
		}
		const FVector2D ToTarget = Target - Shark.Position;
		const float Distance = ToTarget.Size();
		if (Distance > 1.0f)
		{
			const FVector2D Move = ToTarget / Distance * FMath::Min(Speed * DeltaTime, Distance);
			Shark.Position += Move;
			const float WantedHeading = FMath::RadiansToDegrees(FMath::Atan2(ToTarget.Y, ToTarget.X));
			Shark.HeadingDeg = FMath::FixedTurn(Shark.HeadingDeg, WantedHeading, 180.0f * DeltaTime);
		}
	}
	UpdateSharkVisuals();

	if (!Board || bRiderCrashing)
	{
		return;
	}

	// Sand: anything the rider is not above, they hit.
	const float SandHeightCm = GetSandHeightCm(RiderLocation);
	if (SandHeightCm > 0.0f && RiderHeightCm < SandHeightCm + 5.0f)
	{
		FVector BackDir = -Board->Velocity.GetSafeNormal2D();
		if (BackDir.IsNearlyZero())
		{
			BackDir = -Rider->GetActorForwardVector();
		}
		CrashRider(TEXT("Ran aground"), FindWaterNear(RiderLocation, BackDir));
		return;
	}

	// Sharks: on the water within reach, whether riding over one or caught floating.
	if (RiderHeightCm < SharkClearHeightCm)
	{
		for (FSpotShark& Shark : Sharks)
		{
			if (Shark.CalmSeconds <= 0.0f && FVector2D::DistSquared(Shark.Position, RiderXY) <= FMath::Square(SharkBiteRadiusCm))
			{
				// It has had its bite: it leaves the rider alone for a while.
				Shark.CalmSeconds = 8.0f;
				Shark.bHunting = false;
				CrashRider(TEXT("Shark!"), RiderLocation);
				return;
			}
		}
	}
}
