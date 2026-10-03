#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "InputActionValue.h"
#include "BoardMovementComponent.h"
#include "KiteComponent.h"
#include "KiteRiderPawn.h"
#include "KiteSurf.h"
#include "KiteSurfGameMode.h"
#include "KiteSurfUnits.h"
#include "UI/KiteSurfGameInstance.h"
#include "WindComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// Where the player's ride starts the bar. With BAR TO MIDDLE on the bar is spring-loaded about
// BarNeutralSheet, so a ride that started it anywhere else would jump to the middle the first time
// the player touched the sheet input. The game mode's player start (InitializePlayerRide) puts it at
// the middle; the scripted start (InitializeRide, used by tests and lessons) keeps StartSheet.

namespace RideStartBarTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float FrameSeconds = 1.0f / 60.0f;

	/** A pawn in a bare world in steady wind along +X, ticked by hand. */
	struct FStartFixture
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;

		explicit FStartFixture(bool bStepSimulation, float WindKnots = 20.0f)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
			if (Pawn)
			{
				Pawn->bStepSimulation = bStepSimulation;
				if (UWindComponent* Wind = Pawn->GetWind())
				{
					Wind->BaseWind = FVector(KiteUnits::KnotsToCmS(WindKnots), 0.0f, 0.0f);
					Wind->GustStrength = 0.0f;
					Wind->DirectionDriftDeg = 0.0f;
				}
			}
		}

		~FStartFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		/** Ticks this long; returns the largest change of the bar in one frame. */
		float Tick(float Seconds)
		{
			float MaxStep = 0.0f;
			const int32 Steps = FMath::RoundToInt(Seconds / FrameSeconds);
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				const float Before = Sheet();
				Pawn->Tick(FrameSeconds);
				MaxStep = FMath::Max(MaxStep, FMath::Abs(Sheet() - Before));
			}
			return MaxStep;
		}

		void PlayerSheet(float Value) { Pawn->OnSheetTriggered(FInputActionValue(Value)); }

		float Sheet() const { return Pawn->GetCurrentSheetInput(); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfInputRideStartsBarMiddle, "KiteSurf.Input.RideStartsBarMiddle", RideStartBarTest::Flags)

bool FKiteSurfInputRideStartsBarMiddle::RunTest(const FString& Parameters)
{
	using namespace RideStartBarTest;
	const float Speed = KiteUnits::KnotsToCmS(12.0f);

	// BAR TO MIDDLE on, the player's default: the ride starts with the bar at the middle.
	{
		FStartFixture Start(false);
		if (!TestNotNull(TEXT("Pawn spawned"), Start.Pawn))
		{
			return false;
		}
		Start.Pawn->SetBarReturnsToMiddle(true);
		const float Neutral = Start.Pawn->BarNeutralSheet;
		TestNearlyEqual(TEXT("The bar's resting place is the middle"), Neutral, 0.5f, 1e-4f);
		TestNearlyEqual(TEXT("GetPlayerStartSheet with the setting on is BarNeutralSheet"), AKiteSurfGameMode::GetPlayerStartSheet(Start.Pawn, true), Neutral, 1e-4f);

		AKiteSurfGameMode::InitializePlayerRide(Start.Pawn, Speed);
		TestNearlyEqual(TEXT("On: the player's ride starts with the bar at the middle"), Start.Sheet(), Neutral, 1e-4f);
		TestNearlyEqual(TEXT("and the kite has that bar"), Start.Pawn->GetKite()->Sheet, Neutral, 1e-4f);
		TestFalse(TEXT("held there until the player touches the sheet input"), Start.Pawn->IsPlayerSheetInputActive());
		TestEqual(TEXT("on a beam reach, planing"), Start.Pawn->GetBoardMovement()->GetBoardState(), EBoardState::Planing);

		Start.Tick(1.0f);
		TestNearlyEqual(TEXT("Hands off, it stays at the middle"), Start.Sheet(), Neutral, 1e-4f);

		// The first touch: the input's Completed event (0), as a stray tap of the stick sends.
		Start.PlayerSheet(0.0f);
		const float FirstTouchStep = Start.Tick(0.5f);
		TestTrue(TEXT("The first touch hands the bar to the player's spring"), Start.Pawn->IsPlayerSheetInputActive());
		TestNearlyEqual(TEXT("and does not move it"), Start.Sheet(), Neutral, 1e-4f);
		TestTrue(FString::Printf(TEXT("not even for a frame (largest step %.4f)"), FirstTouchStep), FirstTouchStep < 1e-4f);

		// Pulling in moves it smoothly from the middle, at the bar's rate, and letting go returns it.
		Start.PlayerSheet(1.0f);
		const float PullStep = Start.Tick(0.1f);
		TestNearlyEqual(TEXT("Pulled in for 0.1 s: the middle plus 0.1 s of SheetRatePerSec"), Start.Sheet(), Neutral + 0.1f * Start.Pawn->SheetRatePerSec, 0.02f);
		TestTrue(FString::Printf(TEXT("with no jump (largest step %.4f, rate allows %.4f)"), PullStep, Start.Pawn->SheetRatePerSec * FrameSeconds),
			PullStep <= Start.Pawn->SheetRatePerSec * FrameSeconds + 1e-4f);
		Start.PlayerSheet(0.0f);
		Start.Tick(1.0f);
		TestNearlyEqual(TEXT("Let go, it springs back to the middle"), Start.Sheet(), Neutral, 1e-4f);
	}

	// BAR TO MIDDLE off: the old start, held where it is put.
	{
		FStartFixture Start(false);
		if (!TestNotNull(TEXT("Pawn spawned"), Start.Pawn))
		{
			return false;
		}
		Start.Pawn->SetBarReturnsToMiddle(false);
		TestNearlyEqual(TEXT("GetPlayerStartSheet with the setting off is StartSheet"), AKiteSurfGameMode::GetPlayerStartSheet(Start.Pawn, false), AKiteSurfGameMode::StartSheet, 1e-4f);
		AKiteSurfGameMode::InitializePlayerRide(Start.Pawn, Speed);
		TestNearlyEqual(TEXT("Off: the player's ride starts at StartSheet"), Start.Sheet(), AKiteSurfGameMode::StartSheet, 1e-4f);
		Start.PlayerSheet(0.0f);
		Start.Tick(0.5f);
		TestNearlyEqual(TEXT("and a touch of the sheet input leaves it there"), Start.Sheet(), AKiteSurfGameMode::StartSheet, 1e-4f);
	}

	// The scripted start is unchanged whatever the setting: tests and lessons start at StartSheet.
	{
		FStartFixture Start(false);
		if (!TestNotNull(TEXT("Pawn spawned"), Start.Pawn))
		{
			return false;
		}
		Start.Pawn->SetBarReturnsToMiddle(true);
		AKiteSurfGameMode::InitializeRide(Start.Pawn, Speed);
		TestNearlyEqual(TEXT("InitializeRide keeps StartSheet with the setting on"), Start.Sheet(), AKiteSurfGameMode::StartSheet, 1e-4f);
		// What the player's start avoids: from there the first touch jumps the bar to the middle.
		Start.PlayerSheet(0.0f);
		Start.Tick(0.5f);
		TestNearlyEqual(TEXT("(from StartSheet the first touch springs it to the middle)"), Start.Sheet(), Start.Pawn->BarNeutralSheet, 1e-4f);
	}

	// In the game the setting is the game instance's: the pawn's BeginPlay may not have copied it yet.
	for (const bool bGameSetting : { true, false })
	{
		FStartFixture Start(false);
		if (!TestNotNull(TEXT("Pawn spawned"), Start.Pawn))
		{
			return false;
		}
		UKiteSurfGameInstance* GI = NewObject<UKiteSurfGameInstance>(GEngine);
		GI->bBarReturnsToMiddle = bGameSetting;
		GI->bSpotIslands = false;
		GI->bSpotSandbars = false;
		GI->bSpotSharks = false;
		Start.World->SetGameInstance(GI);
		Start.Pawn->SetBarReturnsToMiddle(!bGameSetting);
		AKiteSurfGameMode::InitializePlayerRide(Start.Pawn, Speed);
		const float Expected = bGameSetting ? Start.Pawn->BarNeutralSheet : AKiteSurfGameMode::StartSheet;
		TestNearlyEqual(FString::Printf(TEXT("The game instance's setting (%d) decides, not the pawn's"), bGameSetting ? 1 : 0), Start.Sheet(), Expected, 1e-4f);
		Start.World->SetGameInstance(nullptr);
	}
	return true;
}

// The bar at the middle still rides: in the default gear and wind (the 9 m in 20 kn) a player's ride
// that starts at the middle keeps planing. Held at the start's clock (park-hold, as a rider holding
// the kite low) it settles at a riding speed; hands off, the kite climbs to 12 as it does from any
// start, and the rider is still planing after 5 s.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfRidePlanesFromBarMiddleStart, "KiteSurf.Ride.PlanesFromBarMiddleStart", RideStartBarTest::Flags)

bool FKiteSurfRidePlanesFromBarMiddleStart::RunTest(const FString& Parameters)
{
	using namespace RideStartBarTest;
	const float Speed = KiteUnits::KnotsToCmS(12.0f);

	// Held at the start's clock.
	float HeldSpeedKn[2] = {};
	bool bHeldPlaning[2] = {};
	// Hands off: the seconds until the board drops off the plane (capped at 30).
	float HandsOffPlaningSeconds[2] = {};
	const bool BarToMiddle[2] = { true, false };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		for (const bool bHold : { true, false })
		{
			FStartFixture Start(true);
			if (!TestNotNull(TEXT("Pawn spawned"), Start.Pawn))
			{
				return false;
			}
			UKiteComponent* Kite = Start.Pawn->GetKite();
			UBoardMovementComponent* Board = Start.Pawn->GetBoardMovement();
			Start.Pawn->SetBarReturnsToMiddle(BarToMiddle[Index]);
			AKiteSurfGameMode::InitializePlayerRide(Start.Pawn, Speed);
			Kite->SetKiteSize(9.0f);
			Kite->bParkHoldAssist = bHold;
			if (bHold)
			{
				Start.Tick(20.0f);
				HeldSpeedKn[Index] = KiteUnits::CmSToKnots(Board->Velocity.Size2D());
				bHeldPlaning[Index] = Board->GetBoardState() == EBoardState::Planing && Board->IsPlaning();
				TestFalse(TEXT("The kite stays in the air"), Kite->IsCrashed());
			}
			else
			{
				// The drag regime is worked out by the first step, so step before asking.
				float Seconds = 0.0f;
				do
				{
					Start.Tick(0.25f);
					Seconds += 0.25f;
				} while (Seconds < 30.0f && Board->IsPlaning());
				HandsOffPlaningSeconds[Index] = Seconds;
			}
		}
	}
	UE_LOG(LogKiteSurf, Log, TEXT("PlanesFromBarMiddleStart: 9 m in 20 kn, kite held at the start's clock: bar %.2f %.1f kn (planing %d), bar %.2f %.1f kn (planing %d); hands off, planing for %.2f s from bar %.2f and %.2f s from bar %.2f"),
		0.5f, HeldSpeedKn[0], bHeldPlaning[0] ? 1 : 0, AKiteSurfGameMode::StartSheet, HeldSpeedKn[1], bHeldPlaning[1] ? 1 : 0,
		HandsOffPlaningSeconds[0], 0.5f, HandsOffPlaningSeconds[1], AKiteSurfGameMode::StartSheet);

	TestTrue(FString::Printf(TEXT("Bar at the middle, kite held: still planing after 20 s (%.1f kn)"), HeldSpeedKn[0]), bHeldPlaning[0]);
	TestTrue(FString::Printf(TEXT("at a riding speed (%.1f kn, over 10)"), HeldSpeedKn[0]), HeldSpeedKn[0] > 10.0f);
	TestTrue(FString::Printf(TEXT("slower than from the old start's bar (%.1f against %.1f kn)"), HeldSpeedKn[0], HeldSpeedKn[1]), HeldSpeedKn[0] < HeldSpeedKn[1]);
	TestTrue(FString::Printf(TEXT("Hands off from the middle: still planing after 5 s (%.2f s)"), HandsOffPlaningSeconds[0]), HandsOffPlaningSeconds[0] > 5.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
