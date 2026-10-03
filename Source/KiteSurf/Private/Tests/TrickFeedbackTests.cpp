#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "CoreGlobals.h"
#include "Engine/World.h"
#include "BoardMovementComponent.h"
#include "KiteRiderPawn.h"
#include "Tricks/LandingEvaluator.h"

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

#endif // WITH_DEV_AUTOMATION_TESTS
