#include "KiteSurfGameMode.h"
#include "KiteRiderPawn.h"
#include "KiteSurfHUD.h"
#include "BoardMovementComponent.h"
#include "KiteComponent.h"
#include "WindComponent.h"
#include "UI/KiteSurfGameInstance.h"

AKiteSurfGameMode::AKiteSurfGameMode()
	: InitialSpawnSpeedCmPerSec(12.0f * 51.44f) // 12 knots = ~617.28 cm/s
{
	DefaultPawnClass = AKiteRiderPawn::StaticClass();
	HUDClass = AKiteSurfHUD::StaticClass();
}

void AKiteSurfGameMode::InitializeRide(AKiteRiderPawn* RiderPawn, float InitialSpeedCmPerSec, float TackSide)
{
	if (!RiderPawn)
	{
		return;
	}

	// Synchronize WindComponent with GameInstance PendingWindKnots if set
	if (UWorld* World = RiderPawn->GetWorld())
	{
		if (UKiteSurfGameInstance* GI = Cast<UKiteSurfGameInstance>(World->GetGameInstance()))
		{
			if (UWindComponent* WindComp = RiderPawn->GetWind())
			{
				const float BaseKnots = GI->PendingWindKnots;
				WindComp->BaseWind = FVector(BaseKnots * 51.44f, 0.0f, 0.0f);
			}
		}
	}

	// Start on a beam reach: riding across the wind with the kite powered up low on that side,
	// so the session opens with the rider planing instead of drifting downwind under a parked kite.
	UKiteComponent* Kite = RiderPawn->GetKite();
	const FVector DownwindDir = Kite ? Kite->GetDownwindDir() : FVector::ForwardVector;
	const float Side = TackSide >= 0.0f ? 1.0f : -1.0f;
	const FVector Heading = FVector::CrossProduct(FVector::UpVector, DownwindDir) * Side; // +1: to the right, looking downwind
	RiderPawn->SetActorRotation(Heading.Rotation());

	if (UBoardMovementComponent* BoardMove = RiderPawn->GetBoardMovement())
	{
		BoardMove->Velocity = Heading * InitialSpeedCmPerSec;
		BoardMove->SetBoardState(EBoardState::Planing);
	}

	if (Kite)
	{
		Kite->SetWindowPosition(StartKiteClockDeg * Side, StartKiteDepthDeg);
	}
	RiderPawn->SheetKite(StartSheet);
}

void AKiteSurfGameMode::RestartPlayer(AController* NewPlayer)
{
	Super::RestartPlayer(NewPlayer);
	if (NewPlayer)
	{
		InitializeRide(Cast<AKiteRiderPawn>(NewPlayer->GetPawn()), InitialSpawnSpeedCmPerSec);
	}
}

void AKiteSurfGameMode::RestartPlayerAtPlayerStart(AController* NewPlayer, AActor* StartSpot)
{
	Super::RestartPlayerAtPlayerStart(NewPlayer, StartSpot);
	if (NewPlayer)
	{
		InitializeRide(Cast<AKiteRiderPawn>(NewPlayer->GetPawn()), InitialSpawnSpeedCmPerSec);
	}
}

void AKiteSurfGameMode::RestartPlayerAtTransform(AController* NewPlayer, const FTransform& SpawnTransform)
{
	Super::RestartPlayerAtTransform(NewPlayer, SpawnTransform);
	if (NewPlayer)
	{
		InitializeRide(Cast<AKiteRiderPawn>(NewPlayer->GetPawn()), InitialSpawnSpeedCmPerSec);
	}
}
