#include "KiteSurfGameMode.h"
#include "KiteRiderPawn.h"
#include "KiteSurfHUD.h"
#include "BoardMovementComponent.h"
#include "KiteComponent.h"
#include "WindComponent.h"
#include "UI/KiteSurfGameInstance.h"
#include "KiteSurfSpot.h"
#include "EngineUtils.h"
#include "KiteSurfUnits.h"
#include "School/LessonSubsystem.h"
#include "GameFramework/PlayerController.h"

AKiteSurfGameMode::AKiteSurfGameMode()
	: InitialSpawnSpeedCmPerSec(KiteUnits::KnotsToCmS(12.0f))
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

	// The wind strength chosen on the gear screen. This is the one place a ride's wind is set from
	// the game instance; the wind keeps its direction.
	if (UWorld* World = RiderPawn->GetWorld())
	{
		if (UKiteSurfGameInstance* GI = Cast<UKiteSurfGameInstance>(World->GetGameInstance()))
		{
			if (UWindComponent* WindComp = RiderPawn->GetWind())
			{
				const FVector Direction = WindComp->BaseWind.IsNearlyZero() ? FVector::ForwardVector : WindComp->BaseWind.GetSafeNormal();
				WindComp->BaseWind = Direction * KiteUnits::KnotsToCmS(GI->PendingWindKnots);
			}
			// Rig the chosen kite, or the one a rider would pick for this wind.
			if (UKiteComponent* KiteComp = RiderPawn->GetKite())
			{
				KiteComp->SetKiteModel(GI->KiteModel);
				KiteComp->SetKiteSize(GI->GetEffectiveKiteSizeM2());
			}
			if (UBoardMovementComponent* BoardComp = RiderPawn->GetBoardMovement())
			{
				BoardComp->SetBoardSize(GI->BoardSize);
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

	// What is in the water: laid out round where this ride starts, relative to the wind. Only in a
	// game with a game instance to say what is switched on.
	if (UWorld* World = RiderPawn->GetWorld())
	{
		if (const UKiteSurfGameInstance* GI = Cast<UKiteSurfGameInstance>(World->GetGameInstance()))
		{
			const TActorIterator<AKiteSurfSpot> Existing(World);
			AKiteSurfSpot* Spot = Existing ? *Existing : nullptr;
			if (!Spot)
			{
				Spot = World->SpawnActor<AKiteSurfSpot>();
			}
			if (Spot)
			{
				Spot->Setup(RiderPawn, RiderPawn->GetActorLocation(), DownwindDir, GI->bSpotIslands, GI->bSpotSandbars, GI->bSpotSharks);
			}
		}
	}
}

float AKiteSurfGameMode::GetPlayerStartSheet(const AKiteRiderPawn* RiderPawn, bool bBarReturnsToMiddle)
{
	if (bBarReturnsToMiddle && RiderPawn)
	{
		return FMath::Clamp(RiderPawn->BarNeutralSheet, 0.0f, 1.0f);
	}
	return StartSheet;
}

void AKiteSurfGameMode::InitializePlayerRide(AKiteRiderPawn* RiderPawn, float InitialSpeedCmPerSec, float TackSide)
{
	if (!RiderPawn)
	{
		return;
	}
	InitializeRide(RiderPawn, InitialSpeedCmPerSec, TackSide);

	bool bBarReturnsToMiddle = RiderPawn->GetBarReturnsToMiddle();
	if (const UWorld* World = RiderPawn->GetWorld())
	{
		if (const UKiteSurfGameInstance* GI = Cast<UKiteSurfGameInstance>(World->GetGameInstance()))
		{
			bBarReturnsToMiddle = GI->bBarReturnsToMiddle;
		}
	}
	// Held there as a scripted position: the player's spring takes the bar from the first touch of
	// the sheet input, and with the bar already at the middle that touch does not move it.
	RiderPawn->SheetKite(GetPlayerStartSheet(RiderPawn, bBarReturnsToMiddle));
}

void AKiteSurfGameMode::RestartPlayer(AController* NewPlayer)
{
	Super::RestartPlayer(NewPlayer);
	if (NewPlayer)
	{
		InitializePlayerRide(Cast<AKiteRiderPawn>(NewPlayer->GetPawn()), InitialSpawnSpeedCmPerSec);
	}
}

void AKiteSurfGameMode::RestartPlayerAtPlayerStart(AController* NewPlayer, AActor* StartSpot)
{
	Super::RestartPlayerAtPlayerStart(NewPlayer, StartSpot);
	if (NewPlayer)
	{
		InitializePlayerRide(Cast<AKiteRiderPawn>(NewPlayer->GetPawn()), InitialSpawnSpeedCmPerSec);
	}
}

void AKiteSurfGameMode::RestartPlayerAtTransform(AController* NewPlayer, const FTransform& SpawnTransform)
{
	Super::RestartPlayerAtTransform(NewPlayer, SpawnTransform);
	if (NewPlayer)
	{
		InitializePlayerRide(Cast<AKiteRiderPawn>(NewPlayer->GetPawn()), InitialSpawnSpeedCmPerSec);
	}
}

void AKiteSurfGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
	// Here rather than in RestartPlayer: RestartPlayer runs InitializeRide more than once (through
	// RestartPlayerAtPlayerStart), and the lesson's set-up has to come after the last of them.
	const UGameInstance* GameInstance = GetGameInstance();
	ULessonSubsystem* Lessons = GameInstance ? GameInstance->GetSubsystem<ULessonSubsystem>() : nullptr;
	if (Lessons && NewPlayer && !Lessons->GetPendingLessonId().IsNone())
	{
		Lessons->StartPendingLesson(NewPlayer->GetPawn());
	}
}
