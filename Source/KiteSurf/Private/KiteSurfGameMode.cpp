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

static void InitializePawnVelocity(APawn* Pawn, float InitialSpeedCmPerSec)
{
	if (!Pawn)
	{
		return;
	}
	AKiteRiderPawn* RiderPawn = Cast<AKiteRiderPawn>(Pawn);
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

	// Initialize moving state
	if (UBoardMovementComponent* BoardMove = RiderPawn->GetBoardMovement())
	{
		const FVector Forward2D = RiderPawn->GetActorForwardVector().GetSafeNormal2D();
		BoardMove->Velocity = Forward2D * InitialSpeedCmPerSec;
		BoardMove->SetBoardState(EBoardState::Planing);
	}

	// Initialize kite at 45 deg elevation, azimuth at 0
	if (UKiteComponent* Kite = RiderPawn->GetKite())
	{
		Kite->SetElevationDeg(45.0f);
		Kite->SetAzimuthDeg(0.0f);
	}
}

void AKiteSurfGameMode::RestartPlayer(AController* NewPlayer)
{
	Super::RestartPlayer(NewPlayer);
	if (NewPlayer)
	{
		InitializePawnVelocity(NewPlayer->GetPawn(), InitialSpawnSpeedCmPerSec);
	}
}

void AKiteSurfGameMode::RestartPlayerAtPlayerStart(AController* NewPlayer, AActor* StartSpot)
{
	Super::RestartPlayerAtPlayerStart(NewPlayer, StartSpot);
	if (NewPlayer)
	{
		InitializePawnVelocity(NewPlayer->GetPawn(), InitialSpawnSpeedCmPerSec);
	}
}

void AKiteSurfGameMode::RestartPlayerAtTransform(AController* NewPlayer, const FTransform& SpawnTransform)
{
	Super::RestartPlayerAtTransform(NewPlayer, SpawnTransform);
	if (NewPlayer)
	{
		InitializePawnVelocity(NewPlayer->GetPawn(), InitialSpawnSpeedCmPerSec);
	}
}
