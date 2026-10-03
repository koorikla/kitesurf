#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "BoardMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputActionValue.h"
#include "KiteComponent.h"
#include "KiteRiderPawn.h"
#include "KiteSurfGameMode.h"
#include "KiteSurfHUD.h"
#include "KiteSurfUnits.h"
#include "WindComponent.h"
#include "Tricks/RotationRecognizer.h"
#include "Tricks/TrickTrackerComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// Batch E (docs/tricks/review.md section 4, "show and teach the rotation; the pad send"): batch A's
// IA_Rotate modifier and pre-wind are invisible until taught. This adds a small ROTATE cue while the
// modifier is held, a pre-wind meter next to the load during a modified load, and the trick ticker's
// degrees turned so far. The school's C1/C2 prompts are left for when chapter C exists
// (LessonCatalog.cpp has only ChapterA and ChapterB today), and the pad's LOAD ON LB setting
// (problem 6) is left for the user's choice per the review's risk note; neither is built here.

namespace TrickRotateHUDTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float FrameSeconds = 1.0f / 60.0f;

	/**
	 * A rider on the water in steady wind along +X, with its own HUD (FLiveRide in
	 * TrickLiveRotationTests.cpp, trimmed to what this file needs), so UpdateRotateHUD and
	 * UpdateJumpCard can be driven on a real ride, as docs/tricks/README.md decision 1 asks for a
	 * file of its own per batch.
	 */
	struct FRotateHUDRide
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UKiteComponent* Kite = nullptr;
		UBoardMovementComponent* Board = nullptr;
		UTrickTrackerComponent* Tracker = nullptr;
		USpringArmComponent* Boom = nullptr;
		UWorld* HudWorld = nullptr;
		AKiteSurfHUD* HUD = nullptr;

		explicit FRotateHUDRide(float WindKnots = 30.0f)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
			HudWorld = UWorld::CreateWorld(EWorldType::Game, false);
			HUD = HudWorld ? HudWorld->SpawnActor<AKiteSurfHUD>() : nullptr;
			if (!Pawn)
			{
				return;
			}
			Kite = Pawn->GetKite();
			Board = Pawn->GetBoardMovement();
			Tracker = Pawn->GetTrickTracker();
			Boom = Pawn->FindComponentByClass<USpringArmComponent>();
			if (!Kite || !Board)
			{
				return;
			}
			if (UWindComponent* Wind = Pawn->GetWind())
			{
				Wind->BaseWind = FVector(KiteUnits::KnotsToCmS(WindKnots), 0.0f, 0.0f);
				Wind->GustStrength = 0.0f;
				Wind->DirectionDriftDeg = 0.0f;
			}
			AKiteSurfGameMode::InitializeRide(Pawn, KiteUnits::KnotsToCmS(12.0f), 1.0f);
			Kite->bParkHoldAssist = true;
			Kite->SetKiteModel(EKiteModel::Loop);
			Kite->SetKiteSize(UKiteComponent::RecommendKiteSizeM2(WindKnots));
			if (Boom)
			{
				Boom->bEnableCameraLag = false;
				Boom->bEnableCameraRotationLag = false;
			}
		}

		~FRotateHUDRide()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
			if (HudWorld)
			{
				HudWorld->DestroyWorld(false);
			}
		}

		bool IsValid() const { return Pawn && Kite && Board && Tracker && HUD; }

		void Frame()
		{
			Pawn->Tick(FrameSeconds);
			if (Boom)
			{
				Boom->TickComponent(FrameSeconds, LEVELTICK_All, nullptr);
			}
			HUD->UpdateRotateHUD(Pawn);
			HUD->UpdateJumpCard(Tracker, FrameSeconds);
		}

		bool HasReached(float SimSeconds) const
		{
			return Pawn->GetSimTimeSeconds() + 0.5f * Pawn->SimStepSeconds >= SimSeconds;
		}

		void SimulateUntil(float SimSeconds)
		{
			while (!HasReached(SimSeconds))
			{
				Frame();
			}
		}

		bool IsAirborne() const { return Board->GetBoardState() == EBoardState::Airborne; }

		/** The left stick (or A/D and W/S) as Enhanced Input hands it to the pawn. */
		void Stick(float X, float Y)
		{
			Pawn->OnEdgeTriggered(FInputActionValue(X));
			Pawn->OnWeightShiftTriggered(FInputActionValue(Y));
		}

		void JumpButton(bool bDown)
		{
			if (bDown)
			{
				Pawn->OnJumpPressed(FInputActionValue(true));
			}
			else
			{
				Pawn->OnJumpReleased(FInputActionValue(false));
			}
		}

		/** IA_Rotate: held, the stick reaches the pre-wind and the air rotation (batch A). */
		void Rotate(bool bHeld)
		{
			if (bHeld)
			{
				Pawn->OnRotatePressed(FInputActionValue(true));
			}
			else
			{
				Pawn->OnRotateReleased(FInputActionValue(false));
			}
		}
	};
}

namespace TrickRotateHUDTest
{

// FormatRotateCue and ComputePreWindMeterFraction are pure (review batch E), then a real ride checks
// the HUD actually reads the pawn's IsRotateHeld and GetPreWindAmount through UpdateRotateHUD.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickHUDShowsRotateCue, "KiteSurf.Trick.HUDShowsRotateCue", TrickRotateHUDTest::Flags)

bool FKiteSurfTrickHUDShowsRotateCue::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("No modifier, no cue"), AKiteSurfHUD::FormatRotateCue(false).IsEmpty());
	TestEqual(TEXT("Held, the cue names it"), AKiteSurfHUD::FormatRotateCue(true), FString(TEXT("ROTATE")));

	FRotateHUDRide Ride;
	if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
	{
		return false;
	}
	AKiteRiderPawn* Pawn = Ride.Pawn;
	AKiteSurfHUD* HUD = Ride.HUD;

	HUD->UpdateRotateHUD(nullptr);
	TestTrue(TEXT("With no pawn the cue is hidden"), HUD->GetRotateCueText().IsEmpty());
	TestEqual(TEXT("and the meter reads 0"), HUD->GetPreWindMeterFraction(), 0.0f);

	HUD->UpdateRotateHUD(Pawn);
	TestTrue(TEXT("Before the modifier is pressed the cue is hidden"), HUD->GetRotateCueText().IsEmpty());

	Ride.Rotate(true);
	HUD->UpdateRotateHUD(Pawn);
	TestEqual(TEXT("Pressed, the cue shows"), HUD->GetRotateCueText(), FString(TEXT("ROTATE")));
	TestTrue(TEXT("IsRotateHeld agrees"), Pawn->IsRotateHeld());

	Ride.Rotate(false);
	HUD->UpdateRotateHUD(Pawn);
	TestTrue(TEXT("Released, the cue hides again"), HUD->GetRotateCueText().IsEmpty());
	TestFalse(TEXT("IsRotateHeld agrees"), Pawn->IsRotateHeld());

	return true;
}

// ComputePreWindMeterFraction is pure, then the pre-wind meter fills with GetPreWindAmount while a
// modified load (the jump button and IA_Rotate both held) builds, and hides at once when either one
// is not, even before the pre-wind itself has decayed (batch A's RotateReleaseGraceSeconds).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickPreWindMeterFills, "KiteSurf.Trick.PreWindMeterFills", TrickRotateHUDTest::Flags)

bool FKiteSurfTrickPreWindMeterFills::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Not loading: hidden"), AKiteSurfHUD::ComputePreWindMeterFraction(0.6f, false, true), 0.0f);
	TestEqual(TEXT("No modifier: hidden"), AKiteSurfHUD::ComputePreWindMeterFraction(0.6f, true, false), 0.0f);
	TestEqual(TEXT("Both held: shows the amount"), AKiteSurfHUD::ComputePreWindMeterFraction(0.42f, true, true), 0.42f);
	TestEqual(TEXT("Clamped above 1"), AKiteSurfHUD::ComputePreWindMeterFraction(1.5f, true, true), 1.0f);
	TestEqual(TEXT("Clamped below 0"), AKiteSurfHUD::ComputePreWindMeterFraction(-0.2f, true, true), 0.0f);

	FRotateHUDRide Ride;
	if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
	{
		return false;
	}
	AKiteRiderPawn* Pawn = Ride.Pawn;
	AKiteSurfHUD* HUD = Ride.HUD;

	const float SendAt = 8.0f;
	Ride.SimulateUntil(SendAt);
	Pawn->SteerKite(-1.0f);
	Ride.JumpButton(true); // loading
	HUD->UpdateRotateHUD(Pawn);
	TestEqual(TEXT("Loading with no modifier yet: the meter is hidden"), HUD->GetPreWindMeterFraction(), 0.0f);

	Ride.Rotate(true);
	Ride.Stick(1.0f, 0.0f); // towards a back roll
	Ride.Frame();
	HUD->UpdateRotateHUD(Pawn);
	const float EarlyFraction = HUD->GetPreWindMeterFraction();
	TestTrue(FString::Printf(TEXT("A modified load fills the meter (%.3f)"), EarlyFraction), EarlyFraction > 0.0f && EarlyFraction < 1.0f);
	TestNearlyEqual(TEXT("which tracks GetPreWindAmount"), EarlyFraction, Pawn->GetPreWindAmount(), 0.001f);

	for (int32 Step = 0; Step < 12 && !Ride.IsAirborne(); ++Step)
	{
		Ride.Frame();
	}
	HUD->UpdateRotateHUD(Pawn);
	const float LaterFraction = HUD->GetPreWindMeterFraction();
	TestTrue(FString::Printf(TEXT("It keeps filling (%.3f -> %.3f)"), EarlyFraction, LaterFraction), LaterFraction > EarlyFraction);

	// Letting go of the modifier hides the meter at once, even though the pre-wind amount itself
	// is still held during the release grace.
	Ride.Rotate(false);
	HUD->UpdateRotateHUD(Pawn);
	TestEqual(TEXT("Releasing the modifier hides the meter immediately"), HUD->GetPreWindMeterFraction(), 0.0f);
	TestTrue(TEXT("though the pre-wind amount has not necessarily cleared yet"), Pawn->GetPreWindAmount() > 0.0f);

	Ride.Stick(0.0f, 0.0f);
	Ride.JumpButton(false); // pop, whatever pre-wind survived
	Pawn->SteerKite(0.0f);

	return true;
}

// The ticker's degrees line (review batch E) rides beside the name without changing it: the exact
// moment a roll is first credited still names it "Back roll" with no number
// (KiteSurf.Trick.RecorderCarriesRotation / TrickRecognizerTests.cpp checks the pure function;
// this checks the live HUD path keeps agreeing once the degrees line is added alongside it).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickTickerDegreesFollowRotation, "KiteSurf.Trick.TickerDegreesFollowRotation", TrickRotateHUDTest::Flags)

bool FKiteSurfTrickTickerDegreesFollowRotation::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Below the threshold: no number"), AKiteSurfHUD::FormatTickerDegrees(4.0f).IsEmpty());
	TestEqual(TEXT("Rounded, with the degree sign"), AKiteSurfHUD::FormatTickerDegrees(239.6f), FString(TEXT("240°")));
	TestTrue(TEXT("No tracker: 0 deg"), AKiteSurfHUD::GetLiveRotationDegrees(nullptr) == 0.0f);

	FRotateHUDRide Ride;
	if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
	{
		return false;
	}
	AKiteRiderPawn* Pawn = Ride.Pawn;
	UTrickTrackerComponent* Tracker = Ride.Tracker;
	AKiteSurfHUD* HUD = Ride.HUD;

	const float SendAt = 8.0f;
	Ride.SimulateUntil(SendAt);
	Pawn->SteerKite(-1.0f);
	Ride.Stick(0.0f, -1.0f); // the tail weight, applied before the modifier latches the board
	Ride.JumpButton(true);
	Ride.Rotate(true);
	Ride.Stick(1.0f, 0.0f); // towards a back roll
	const float ReleaseAt = SendAt + FMath::RoundToFloat((0.66f + Ride.Kite->GetSteeringDeadTimeSeconds()) * 30.0f) / 30.0f;
	while (!Ride.HasReached(ReleaseAt) && !Ride.IsAirborne())
	{
		Ride.Frame();
	}
	Pawn->SheetKite(1.0f);
	Ride.JumpButton(false); // pop
	Ride.Stick(0.0f, 0.0f);
	Ride.Rotate(false);
	Pawn->SteerKite(0.0f);

	bool bSawDegrees = false;
	bool bTickerStayedBackRollWhenNamed = true;
	float MaxDegreesSeen = 0.0f;
	for (int32 Step = 0; Step < 180 && Ride.IsAirborne(); ++Step)
	{
		Ride.Frame();
		const FString Ticker = HUD->GetTrickTickerText();
		const FString Degrees = HUD->GetTrickTickerDegreesText();
		if (Ticker == TEXT("Back roll") && !Degrees.IsEmpty())
		{
			bSawDegrees = true;
			MaxDegreesSeen = FMath::Max(MaxDegreesSeen, AKiteSurfHUD::GetLiveRotationDegrees(Tracker));
		}
		else if (!Ticker.IsEmpty() && Ticker != TEXT("Back roll"))
		{
			bTickerStayedBackRollWhenNamed = false;
		}
	}

	TestTrue(TEXT("The ticker named the back roll with a degrees line beside it at some point"), bSawDegrees);
	TestTrue(TEXT("Once named it stayed 'Back roll' (unaffected by the degrees addition)"), bTickerStayedBackRollWhenNamed);
	TestTrue(FString::Printf(TEXT("The degrees turned climbed past a sensible starting point (%.0f deg)"), MaxDegreesSeen), MaxDegreesSeen > 100.0f);

	return true;
}

}

#endif // WITH_DEV_AUTOMATION_TESTS
