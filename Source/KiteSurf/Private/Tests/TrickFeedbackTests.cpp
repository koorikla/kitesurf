#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "CoreGlobals.h"
#include "Engine/World.h"
#include "BoardMovementComponent.h"
#include "KiteComponent.h"
#include "KiteRiderPawn.h"
#include "KiteSurfGameMode.h"
#include "KiteSurfHUD.h"
#include "KiteSurfUnits.h"
#include "WindComponent.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/LandingEvaluator.h"
#include "Tricks/TrickTrackerComponent.h"
#include "UI/KiteSurfGameInstance.h"
#include "UI/KiteSurfSaveGame.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

// Batch D (docs/tricks/review.md section 4): feedback for a landed trick, and the trick book.
// Problems 5 and 7 (section 3): nothing but the HUD text reacted to a trick, and the trick book was
// not saved when a trick was first landed.
//
// These tests spawn a pawn into a bare world and call AActor::DispatchBeginPlay() directly, which is
// what actually wires AKiteRiderPawn::BeginPlay's delegate bindings (OnBoardLandingVerdict,
// OnBoardCrash, ...): a plain SpawnActor into a world that never called UWorld::BeginPlay never runs
// an actor's BeginPlay (AActor::DispatchBeginPlay only dispatches once World->HasBegunPlay(), unless
// called directly as here). Broadcasting a dynamic multicast delegate afterwards still would not
// reach the bound handler: AActor::ProcessEvent refuses every call while
// World->AreActorsInitialized() is false (true only after UWorld::InitializeActorsForPlay, which a
// bare CreateWorld world never runs) unless GAllowActorScriptExecutionInEditor is set - the editor's
// own escape hatch for exactly this, used the same way by Engine/Source/Developer/FunctionalTesting.
// No other test in this suite broadcasts on one of the pawn's bound delegates, which is why this gap
// was never hit before.

namespace TrickFeedbackTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** A pawn in a throwaway world, with BeginPlay's delegate bindings wired and actually reachable. */
	struct FFeedbackFixture
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UBoardMovementComponent* Board = nullptr;
		TGuardValue<bool> ScriptExecutionGuard;

		FFeedbackFixture()
			: ScriptExecutionGuard(GAllowActorScriptExecutionInEditor, true)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
			if (Pawn)
			{
				Pawn->DispatchBeginPlay();
				Board = Pawn->GetBoardMovement();
			}
		}

		~FFeedbackFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		bool IsValid() const { return World && Pawn && Board; }
	};

	FLandingVerdict MakeVerdict(ELandingGrade Grade, ELandingCause Cause = ELandingCause::None)
	{
		FLandingVerdict Verdict;
		Verdict.Grade = Grade;
		Verdict.Cause = Cause;
		return Verdict;
	}

	constexpr float FrameSeconds = 1.0f / 60.0f;

	/**
	 * A rider on the water in steady wind along +X (FLiveRide in TrickLiveRotationTests.cpp, trimmed
	 * to what TrickBookSavedOnUnlock needs: no HUD, no camera).
	 */
	struct FTrickRide
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UKiteComponent* Kite = nullptr;
		UBoardMovementComponent* Board = nullptr;
		UTrickTrackerComponent* Tracker = nullptr;

		explicit FTrickRide(float WindKnots = 30.0f)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
			if (!Pawn)
			{
				return;
			}
			Kite = Pawn->GetKite();
			Board = Pawn->GetBoardMovement();
			Tracker = Pawn->GetTrickTracker();
			if (!Kite || !Board || !Tracker)
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
		}

		~FTrickRide()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		bool IsValid() const { return World && Pawn && Kite && Board && Tracker; }
		bool HasReached(float SimSeconds) const { return Pawn->GetSimTimeSeconds() + 0.5f * Pawn->SimStepSeconds >= SimSeconds; }
		void Frame() { Pawn->Tick(FrameSeconds); }
		void SimulateUntil(float SimSeconds) { while (!HasReached(SimSeconds)) { Frame(); } }
		bool IsAirborne() const { return Board->GetBoardState() == EBoardState::Airborne; }
	};

	/**
	 * TrickLiveRotationTests' timed 30 kn jump with no pre-wind ("Straight air"): the bar hard over,
	 * weight on the tail, the jump button held and let go 0.66 s after the bar reaches the kite, a
	 * crouch for the landing. Reliably lands Clean (batch C: KiteSurf.Trick.CardGradeMatchesVerdict).
	 * SendAt is the sim time (s) to start the send from, so the same ride can fly it twice in a row.
	 */
	bool FlyStraightAirJump(FTrickRide& Ride, float SendAt)
	{
		AKiteRiderPawn* Pawn = Ride.Pawn;
		Ride.SimulateUntil(SendAt);
		Pawn->SteerKite(-1.0f);
		Ride.Board->SetWeightShift(-1.0f);
		Pawn->SetLoadHeld(true);
		Pawn->SetPreWind(FVector2D::ZeroVector);
		const float ReleaseAt = SendAt + FMath::RoundToFloat((0.66f + Ride.Kite->GetSteeringDeadTimeSeconds()) * 30.0f) / 30.0f;
		while (!Ride.HasReached(ReleaseAt) && !Ride.IsAirborne())
		{
			Ride.Frame();
		}
		Pawn->SheetKite(1.0f);
		Pawn->ReleaseLoadAndPop();
		Ride.Board->SetWeightShift(0.0f);
		Pawn->SteerKite(0.0f);

		bool bWasAir = false;
		bool bLanded = false;
		const float EndAt = ReleaseAt + 20.0f;
		while (!Ride.HasReached(EndAt))
		{
			Ride.Frame();
			if (Ride.IsAirborne())
			{
				bWasAir = true;
				if (Ride.Board->Velocity.Z < 0.0f)
				{
					Pawn->SetLoadHeld(true); // coming down: crouch for the landing
				}
			}
			else if (bWasAir)
			{
				bLanded = Ride.Board->WasLastLandingClean();
				break;
			}
		}
		Pawn->SetLoadHeld(false);
		return bLanded;
	}

	/**
	 * Backs up the player's real Settings save (if any) before a test that must exercise the real
	 * disk path - UKiteSurfGameInstance::SaveSettingsToDisk always writes the default slot, with no
	 * way to redirect it to a test slot - and restores it byte for byte afterward, or deletes the
	 * slot if it did not exist before. The same path SchoolOnboardingTests.cpp's FTutorialSettingsGuard
	 * reads to prove a save is unchanged; this one expects a change, and undoes it.
	 */
	struct FDefaultSlotGuard
	{
		FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), UKiteSurfSaveGame::DefaultSaveSlot + TEXT(".sav"));
		bool bExisted = false;
		TArray<uint8> Bytes;

		FDefaultSlotGuard()
		{
			// FileExists first: LoadFileToArray on a missing file logs an engine warning (the slot
			// is usually empty - no settings save has been written in this worktree yet).
			bExisted = IFileManager::Get().FileExists(*Path);
			if (bExisted)
			{
				FFileHelper::LoadFileToArray(Bytes, *Path);
			}
		}

		~FDefaultSlotGuard()
		{
			if (bExisted)
			{
				FFileHelper::SaveArrayToFile(Bytes, *Path);
			}
			else
			{
				IFileManager::Get().Delete(*Path);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickStompedLandingRumbles, "KiteSurf.Trick.StompedLandingRumbles", TrickFeedbackTest::Flags)

bool FKiteSurfTrickStompedLandingRumbles::RunTest(const FString& Parameters)
{
	using namespace TrickFeedbackTest;
	FFeedbackFixture Fixture;
	if (!TestTrue(TEXT("Fixture created"), Fixture.IsValid()))
	{
		return false;
	}

	// A Stomped landing: a heavy buzz, a short camera kick and its own one-shot, on top of whatever
	// HandleBoardLanding already did for the g (not broadcast here, so isolated).
	const int32 HapticsBefore = Fixture.Pawn->GetHapticCount();
	const float KickBefore = Fixture.Pawn->GetCameraKickDeg();
	TestEqual(TEXT("No camera kick yet"), KickBefore, 0.0f);

	Fixture.Board->OnBoardLandingVerdict.Broadcast(MakeVerdict(ELandingGrade::Stomped));

	TestEqual(TEXT("A stomped landing buzzes once"), Fixture.Pawn->GetHapticCount(), HapticsBefore + 1);
	TestTrue(FString::Printf(TEXT("on the heavy motors, at close to full strength (%.2f)"), Fixture.Pawn->GetLastHapticIntensity()),
		Fixture.Pawn->GetLastHapticIntensity() >= 0.9f);
	const float KickAfterStomped = Fixture.Pawn->GetCameraKickDeg();
	TestTrue(FString::Printf(TEXT("A camera kick fires (%.2f deg)"), KickAfterStomped), KickAfterStomped > 0.0f);
	TestTrue(TEXT("and the stomp one-shot plays"), Fixture.Pawn->GetLastOneShotVolume() > 0.0f);

	// A Sketchy landing: a light wobble, no extra camera kick, no stomp one-shot.
	const int32 HapticsBeforeSketchy = Fixture.Pawn->GetHapticCount();
	const float VolumeBeforeSketchy = Fixture.Pawn->GetLastOneShotVolume();
	Fixture.Board->OnBoardLandingVerdict.Broadcast(MakeVerdict(ELandingGrade::Sketchy, ELandingCause::UnderRotated));
	TestEqual(TEXT("A sketchy landing buzzes once too"), Fixture.Pawn->GetHapticCount(), HapticsBeforeSketchy + 1);
	TestTrue(FString::Printf(TEXT("but lightly (%.2f), not on the heavy motors' full strength"), Fixture.Pawn->GetLastHapticIntensity()),
		Fixture.Pawn->GetLastHapticIntensity() < 0.5f);
	TestEqual(TEXT("A sketchy landing does not add to the camera kick"), Fixture.Pawn->GetCameraKickDeg(), KickAfterStomped);
	TestEqual(TEXT("and no extra one-shot"), Fixture.Pawn->GetLastOneShotVolume(), VolumeBeforeSketchy);

	// A Clean landing: nothing extra here (HandleBoardLanding, bound to OnBoardLanding, is clean's
	// own g-scaled feedback and is not exercised by this delegate).
	const int32 HapticsBeforeClean = Fixture.Pawn->GetHapticCount();
	Fixture.Board->OnBoardLandingVerdict.Broadcast(MakeVerdict(ELandingGrade::Clean));
	TestEqual(TEXT("A clean landing adds nothing on this delegate"), Fixture.Pawn->GetHapticCount(), HapticsBeforeClean);
	TestEqual(TEXT("and does not touch the camera kick either"), Fixture.Pawn->GetCameraKickDeg(), KickAfterStomped);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickCrashLandingThuds, "KiteSurf.Trick.CrashLandingThuds", TrickFeedbackTest::Flags)

bool FKiteSurfTrickCrashLandingThuds::RunTest(const FString& Parameters)
{
	using namespace TrickFeedbackTest;
	FFeedbackFixture Fixture;
	if (!TestTrue(TEXT("Fixture created"), Fixture.IsValid()))
	{
		return false;
	}

	// The board broadcasts OnBoardLandingVerdict with a Crash grade, then OnBoardCrash, on every
	// crash landing (UBoardMovementComponent::StepBoard). The verdict alone must add nothing (Crash
	// is HandleBoardCrash's job, unchanged): only the existing crash thump fires, from OnBoardCrash.
	const int32 HapticsBefore = Fixture.Pawn->GetHapticCount();
	Fixture.Board->OnBoardLandingVerdict.Broadcast(MakeVerdict(ELandingGrade::Crash, ELandingCause::Inverted));
	TestEqual(TEXT("The crash verdict alone does not buzz"), Fixture.Pawn->GetHapticCount(), HapticsBefore);
	TestEqual(TEXT("and does not kick the camera"), Fixture.Pawn->GetCameraKickDeg(), 0.0f);

	Fixture.Board->OnBoardCrash.Broadcast(1.0f);
	TestEqual(TEXT("The crash itself still thuds once, as before"), Fixture.Pawn->GetHapticCount(), HapticsBefore + 1);
	TestEqual(TEXT("heavy and at full strength"), Fixture.Pawn->GetLastHapticIntensity(), 1.0f);
	TestEqual(TEXT("for the same duration as before (0.45 s)"), Fixture.Pawn->GetLastHapticDuration(), 0.45f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickRotationWhooshFollowsSpin, "KiteSurf.Trick.RotationWhooshFollowsSpin", TrickFeedbackTest::Flags)

bool FKiteSurfTrickRotationWhooshFollowsSpin::RunTest(const FString& Parameters)
{
	// Pure math, as KiteSurf.Audio.MixFollowsTheRide checks the other loops: ComputeAudioMix needs
	// no world or pawn. A rotation whoosh whose pitch follows the spin rate (tricks.md 6.9).
	FRideAudioState Grounded;
	Grounded.bAirborne = false;
	Grounded.bOnWater = true;
	Grounded.SpinRadS = 5.0f; // the attitude only simulates in the air; this should not happen, but silence proves the gate.
	TestEqual(TEXT("No rotation whoosh on the water, however SpinRadS reads"), AKiteRiderPawn::ComputeAudioMix(Grounded).RotationVolume, 0.0f);

	FRideAudioState Still;
	Still.bAirborne = true;
	Still.bOnWater = false;
	Still.SpinRadS = 0.0f;
	const FRideAudioMix NoSpin = AKiteRiderPawn::ComputeAudioMix(Still);
	TestEqual(TEXT("Airborne with no rotation: silent"), NoSpin.RotationVolume, 0.0f);

	FRideAudioState Gentle = Still;
	Gentle.SpinRadS = 1.5f; // a slow wobble
	const FRideAudioMix GentleMix = AKiteRiderPawn::ComputeAudioMix(Gentle);
	TestTrue(FString::Printf(TEXT("A gentle spin is quiet but audible (%.2f)"), GentleMix.RotationVolume), GentleMix.RotationVolume > 0.0f && GentleMix.RotationVolume < 0.3f);
	TestTrue(FString::Printf(TEXT("and a little higher than resting pitch (%.2f)"), GentleMix.RotationPitch), GentleMix.RotationPitch > NoSpin.RotationPitch);

	FRideAudioState Fast = Still;
	Fast.SpinRadS = 4.0f; // a committed roll (RiderAttitudeComponent's PreWindRollRateDegS 250 deg/s ~= 4.4 rad/s)
	const FRideAudioMix FastMix = AKiteRiderPawn::ComputeAudioMix(Fast);
	TestTrue(FString::Printf(TEXT("A fast spin is louder and higher than a gentle one (vol %.2f > %.2f, pitch %.2f > %.2f)"),
		FastMix.RotationVolume, GentleMix.RotationVolume, FastMix.RotationPitch, GentleMix.RotationPitch),
		FastMix.RotationVolume > GentleMix.RotationVolume && FastMix.RotationPitch > GentleMix.RotationPitch);

	FRideAudioState Fastest = Still;
	Fastest.SpinRadS = 20.0f; // far past any rate the game ever commands
	const FRideAudioMix FastestMix = AKiteRiderPawn::ComputeAudioMix(Fastest);
	TestTrue(TEXT("Nothing is louder than full volume, however fast the spin reads"), FastestMix.RotationVolume <= 0.75f + 1e-4f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickNewTrickNoticeOnce, "KiteSurf.Trick.NewTrickNoticeOnce", TrickFeedbackTest::Flags)

bool FKiteSurfTrickNewTrickNoticeOnce::RunTest(const FString& Parameters)
{
	// HUD-level, as KiteSurf.Trick.CardGradeMatchesVerdict checks AKiteSurfHUD::FormatJumpCard
	// directly: ShowJumpCard's bIsNewTrick is UpdateJumpCard passing through
	// UTrickTrackerComponent::WasLastLandingNewTrick (review batch D, problem 7).
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AKiteSurfHUD* HUD = World ? World->SpawnActor<AKiteSurfHUD>() : nullptr;
	if (!TestNotNull(TEXT("HUD spawned"), HUD))
	{
		return false;
	}

	TestTrue(TEXT("No notice before any jump card"), HUD->GetNewTrickNoticeText().IsEmpty());

	FJumpRecord BackRoll;
	BackRoll.TrickName = TEXT("Back roll");
	BackRoll.ApexHeightCm = 500.0f;
	BackRoll.Grade = ELandingGrade::Clean;

	// The first landing of a trick: the notice names it.
	HUD->ShowJumpCard(BackRoll, true);
	TestEqual(TEXT("The notice names the newly landed trick"), HUD->GetNewTrickNoticeText(), FString(TEXT("NEW TRICK: Back roll")));

	// A repeat of the same trick: the card is still shown, but with no notice.
	HUD->ShowJumpCard(BackRoll, false);
	TestTrue(TEXT("A repeat shows no notice"), HUD->GetNewTrickNoticeText().IsEmpty());
	TestFalse(TEXT("but the card itself is still up"), HUD->GetJumpCardText().IsEmpty());

	// A different trick, also landed for the first time: its own notice.
	FJumpRecord FrontRoll;
	FrontRoll.TrickName = TEXT("Front roll");
	FrontRoll.ApexHeightCm = 400.0f;
	FrontRoll.Grade = ELandingGrade::Stomped;
	HUD->ShowJumpCard(FrontRoll, true);
	TestEqual(TEXT("A different new trick gets its own notice"), HUD->GetNewTrickNoticeText(), FString(TEXT("NEW TRICK: Front roll")));

	// The notice goes with the card: once that clears (no tracker, as with no pawn), so does the notice.
	HUD->UpdateJumpCard(nullptr, 0.0f);
	TestTrue(TEXT("No card once the tracker is gone"), HUD->GetJumpCardText().IsEmpty());
	TestTrue(TEXT("and so no notice either"), HUD->GetNewTrickNoticeText().IsEmpty());

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickTrickBookSavedOnUnlock, "KiteSurf.Trick.TrickBookSavedOnUnlock", TrickFeedbackTest::Flags)

bool FKiteSurfTrickTrickBookSavedOnUnlock::RunTest(const FString& Parameters)
{
	using namespace TrickFeedbackTest;

	// Guards the real Settings slot for the whole test: UTrickTrackerComponent::StepTracker calls
	// UKiteSurfGameInstance::SaveSettingsToDisk, which always writes it, with no test slot to redirect
	// to. The guard restores whatever was there (or removes the slot) when the test ends.
	FDefaultSlotGuard SlotGuard;

	FTrickRide Ride(30.0f);
	if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
	{
		return false;
	}
	UKiteSurfGameInstance* GameInstance = NewObject<UKiteSurfGameInstance>(GEngine);
	Ride.World->SetGameInstance(GameInstance);

	if (!TestTrue(TEXT("The first jump lands"), FlyStraightAirJump(Ride, 8.0f)))
	{
		Ride.World->SetGameInstance(nullptr);
		return false;
	}
	TestEqual(TEXT("One jump recorded"), Ride.Tracker->GetJumpRecordCount(), 1);
	TestTrue(TEXT("The first landing of this trick is new"), Ride.Tracker->WasLastLandingNewTrick());
	TestEqual(TEXT("The game instance's book has it"), GameInstance->GetTrickBook().Num(), 1);

	// A fresh read, never into SlotGuard.Bytes: that is the pre-test backup its destructor restores.
	TArray<uint8> BytesAfterFirst;
	const bool bSavedAfterFirst = FFileHelper::LoadFileToArray(BytesAfterFirst, *SlotGuard.Path);
	TestTrue(TEXT("Unlocking a trick reaches the real Settings slot (SaveSettingsToDisk)"), bSavedAfterFirst);
	const FDateTime StampAfterFirst = IFileManager::Get().GetTimeStamp(*SlotGuard.Path);
	if (TestTrue(TEXT("The saved book has the trick"), bSavedAfterFirst))
	{
		const UKiteSurfSaveGame* FromDisk = UKiteSurfSaveGame::LoadOrCreateSettings();
		TestTrue(TEXT("...read back from the default slot"), FromDisk && FromDisk->TrickBook.Num() == 1);
	}

	// A repeat of the same trick: not new, nothing extra saved (the slot's own timestamp is untouched).
	const float SecondSendAt = Ride.Pawn->GetSimTimeSeconds() + 8.0f;
	if (!TestTrue(TEXT("The second jump lands"), FlyStraightAirJump(Ride, SecondSendAt)))
	{
		Ride.World->SetGameInstance(nullptr);
		return false;
	}
	TestEqual(TEXT("Two jumps recorded"), Ride.Tracker->GetJumpRecordCount(), 2);
	TestFalse(TEXT("A repeat landing of the same trick is not new"), Ride.Tracker->WasLastLandingNewTrick());
	TestEqual(TEXT("Still one trick in the book"), GameInstance->GetTrickBook().Num(), 1);
	TestEqual(TEXT("No extra write to the Settings slot for a repeat"), IFileManager::Get().GetTimeStamp(*SlotGuard.Path), StampAfterFirst);

	Ride.World->SetGameInstance(nullptr);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
