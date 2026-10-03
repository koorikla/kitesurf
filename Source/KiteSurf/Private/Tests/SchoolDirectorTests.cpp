#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "BoardMovementComponent.h"
#include "KiteComponent.h"
#include "KiteRiderPawn.h"
#include "KiteSurfUnits.h"
#include "WindComponent.h"
#include "School/LessonCatalog.h"
#include "School/LessonDirector.h"
#include "School/LessonSubsystem.h"
#include "Tricks/RiderAttitudeComponent.h"
#include "Tricks/TrickTrackerComponent.h"
#include "UI/KiteSurfGameInstance.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the kite school's lesson director (docs/tutorials.md S3): a pawn in a throwaway world,
// stepped frame by frame at 60 fps, with an ALessonDirector updated after each pawn tick as its own
// tick would be in the game. Results go to a ULessonSubsystem that never writes to disk.
namespace SchoolDirectorTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	const float DirectorFrameSeconds = 1.0f / 60.0f;

	struct FDirectorFixture
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		ALessonDirector* Director = nullptr;
		ULessonSubsystem* Lessons = nullptr;

		FDirectorFixture()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (World && GEngine)
			{
				// A context, so the director can be destroyed (ExitToFreeRide) as in a game.
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			}
			Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
			if (Pawn)
			{
				Pawn->bInterpolateRendering = false;
				if (UWindComponent* Wind = Pawn->GetWind())
				{
					Wind->BaseWind = FVector(KiteUnits::KnotsToCmS(20.0f), 0.0f, 0.0f);
					Wind->GustStrength = 0.0f;
					Wind->DirectionDriftDeg = 0.0f;
				}
			}
			Director = World ? World->SpawnActor<ALessonDirector>() : nullptr;
			Lessons = NewObject<ULessonSubsystem>(NewObject<UKiteSurfGameInstance>());
			Lessons->SetWriteToDisk(false);
			Lessons->SetTravelEnabled(false);
			if (Director)
			{
				Director->SetLessonSubsystem(Lessons);
				Director->IntroSeconds = 0.0f;
			}
		}

		~FDirectorFixture()
		{
			if (World)
			{
				if (GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}
				World->DestroyWorld(false);
			}
		}

		bool IsValid() const { return Pawn && Director && Lessons; }

		void Frame()
		{
			Pawn->Tick(DirectorFrameSeconds);
			Director->UpdateLesson(DirectorFrameSeconds);
		}
	};

	/**
	 * A scripted rider for the jump lessons (B2 at 14 kn on the Boost): ride a few seconds with the
	 * kite low on the tack's side, send it to 12 with the bar part out, hold the edge loaded, pop once
	 * the kite has been over PopElevationDeg for a moment with the bar in, keep the kite at 12 in the
	 * air and crouch on the way down; then the same again. The send-and-pop of the trick ride fixtures
	 * (TrickTrackerTests.cpp, RideLoopTests.cpp) adapted to light wind: the kite is flown there with a
	 * damped hand rather than a full-lock send. With the defaults it lands 1.3 to 1.7 m jumps Clean
	 * (tuned under -nullrhi on the physics of 2026-10-03).
	 */
	struct FJumpRider
	{
		enum class EPhase { Ride, Send, Air };
		EPhase Phase = EPhase::Ride;
		float PhaseSeconds = 0.0f;
		float AirSeconds = 0.0f;
		/** Seconds of riding with the kite low before each send. */
		float RideSeconds = 5.0f;
		/** Clock the kite is held at while riding (deg, on the tack's side): low, for power. */
		float RideClockDeg = 65.0f;
		/** Bar while the kite climbs: part out, under LessonRules::SheetedInBar. */
		float SendBar = 0.6f;
		float RideBar = 0.7f;
		/** Edge (carve) input held while riding and loading: against the pull on this tack. */
		float Edge = 0.8f;
		/** Damping of the hand on the bar: it steers against the kite's clock rate times this (s). */
		float SteerDampingSeconds = 0.6f;
		float LastClock = 0.0f;
		bool bHasLastClock = false;
		/** Steering of the send: degrees of clock per unit of bar, and the most bar. */
		float SendGainDeg = 25.0f;
		float SendMaxSteer = 0.8f;
		/** Above 0: bar right in once the kite is this high, while it still climbs (deg). */
		float BadSheetInDeg = -1.0f;
		/** Bar at the pop, with the kite at 12. */
		float PopBar = 0.8f;
		/** Weight along the board while loading for the pop (-1 on the tail). */
		float LoadWeight = -1.0f;
		/** The kite stays over PopElevationDeg this long before the pop (s). */
		float PopHoldSeconds = 0.1f;
		float AtTopSeconds = 0.0f;
		/** Longest send before popping anyway (s). */
		float MaxSendSeconds = 6.0f;
		/** Pop when the kite is this high (deg). */
		float PopElevationDeg = 80.0f;
		/** The tack's side of the window: +1 the kite on the right (InitializeRide's tack +1). */
		float Side = 1.0f;
		int32 Sends = 0;
		/** While this director is on its first step ("send the kite to 12"), hold the send until it moves on. */
		const ALessonDirector* WaitOnFirstStep = nullptr;
		bool bWaited = false;

		void Step(AKiteRiderPawn* Pawn, float Dt)
		{
			UKiteComponent* Kite = Pawn->GetKite();
			UBoardMovementComponent* Board = Pawn->GetBoardMovement();
			const float Clock = Kite->GetClockDeg();
			const float ClockRate = bHasLastClock && Dt > 0.0f ? (Clock - LastClock) / Dt : 0.0f;
			LastClock = Clock;
			bHasLastClock = true;
			// A damped hand on the bar: towards a clock position, easing off as the kite gets there.
			auto SteerTo = [&](float TargetClock, float GainDeg, float MaxSteer)
			{
				Pawn->SteerKite(FMath::Clamp((TargetClock - Clock - SteerDampingSeconds * ClockRate) / GainDeg, -MaxSteer, MaxSteer));
			};
			PhaseSeconds += Dt;
			const bool bAir = Board->GetBoardState() == EBoardState::Airborne;
			switch (Phase)
			{
			case EPhase::Ride:
				SteerTo(RideClockDeg * Side, 30.0f, 0.6f);
				Pawn->SheetKite(RideBar);
				Pawn->EdgeBoard(Edge);
				if (PhaseSeconds >= RideSeconds && !bAir && !Board->IsCrashing())
				{
					Phase = EPhase::Send;
					PhaseSeconds = 0.0f;
					AtTopSeconds = 0.0f;
					++Sends;
				}
				break;
			case EPhase::Send:
				SteerTo(0.0f, SendGainDeg, SendMaxSteer);
				// The B2 mistake, when asked for: bar in while the kite is still climbing.
				Pawn->SheetKite(BadSheetInDeg > 0.0f && Kite->GetElevationDeg() >= BadSheetInDeg ? 1.0f : SendBar);
				Pawn->EdgeBoard(Edge);
				Board->SetWeightShift(LoadWeight);
				Pawn->SetLoadHeld(true);
				AtTopSeconds = Kite->GetElevationDeg() >= PopElevationDeg ? AtTopSeconds + Dt : 0.0f;
				if (WaitOnFirstStep && WaitOnFirstStep->GetPhase() == ELessonPhase::Step && WaitOnFirstStep->GetStepIndex() == 0)
				{
					Pawn->SheetKite(SendBar);
					// The first step asks for the kite at 12 and nothing else: ride on and fly it up with the bar out.
					Pawn->SetLoadHeld(false);
					Board->SetWeightShift(0.0f);
					bWaited = true;
					break;
				}
				if (bWaited)
				{
					// The first step is done with the kite at the top: ride again and start a send from low down.
					bWaited = false;
					Phase = EPhase::Ride;
					PhaseSeconds = 0.0f;
					break;
				}
				if (AtTopSeconds >= PopHoldSeconds || PhaseSeconds > MaxSendSeconds)
				{
					Pawn->SheetKite(PopBar);
					Pawn->ReleaseLoadAndPop();
					Board->SetWeightShift(0.0f);
					Phase = EPhase::Air;
					PhaseSeconds = 0.0f;
					AirSeconds = 0.0f;
				}
				break;
			case EPhase::Air:
				SteerTo(0.0f, 40.0f, 0.3f);
				if (bAir)
				{
					AirSeconds += Dt;
					if (Board->Velocity.Z < 0.0f)
					{
						Pawn->SetLoadHeld(true);
					}
				}
				if ((!bAir && AirSeconds > 0.2f) || PhaseSeconds > 6.0f)
				{
					Pawn->SetLoadHeld(false);
					Phase = EPhase::Ride;
					PhaseSeconds = 0.0f;
				}
				break;
			}
		}
	};

	/** The B2 mistake: bar right in while the kite still climbs through 45 deg, and a pop at 65 deg before it reaches 12. Lands 2.3 m jumps: too high for B2. */
	FJumpRider BadSendRider(const ALessonDirector* Director)
	{
		FJumpRider Rider;
		Rider.WaitOnFirstStep = Director;
		Rider.BadSheetInDeg = 45.0f;
		Rider.PopElevationDeg = 65.0f;
		Rider.PopBar = 1.0f;
		return Rider;
	}

	/** Rides until Done says so or Seconds run out; false on timeout. */
	template <typename FDone>
	bool RideUntil(FDirectorFixture& Fx, FJumpRider& Rider, float Seconds, FDone&& Done)
	{
		for (float T = 0.0f; T < Seconds; T += DirectorFrameSeconds)
		{
			Rider.Step(Fx.Pawn, DirectorFrameSeconds);
			Fx.Frame();
			if (Done())
			{
				return true;
			}
		}
		return false;
	}
}

using namespace SchoolDirectorTest;

// docs/tutorials.md S3: a scripted ride passes lesson B2 (five 1 to 2 m jumps landed Clean in a row)
// on a pawn under -nullrhi, the director reports the pass with its stars and records it in the
// progress book, and Next goes on to B3.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolDirectorPassesB2, "KiteSurf.School.DirectorPassesB2", SchoolDirectorTest::Flags)

bool FKiteSurfSchoolDirectorPassesB2::RunTest(const FString& Parameters)
{
	FDirectorFixture Fx;
	const FLessonDef* B2 = LessonCatalog::Find(TEXT("B2"));
	if (!TestTrue(TEXT("Fixture and lesson B2"), Fx.IsValid() && B2))
	{
		return false;
	}
	TestTrue(TEXT("B2 begins on the rider"), Fx.Director->BeginLesson(*B2, Fx.Pawn));
	TestEqual(TEXT("B2 is the lesson"), Fx.Director->GetLessonId(), FName(TEXT("B2")));

	FJumpRider Rider;
	Rider.WaitOnFirstStep = Fx.Director;
	const bool bFirstStep = RideUntil(Fx, Rider, 40.0f, [&] { return Fx.Director->GetStepIndex() == 1; });
	TestTrue(TEXT("Step 1 (send the kite to 12) is met: the kite went over the top (LessonTiming::SheetPerfectMinDeg)"), bFirstStep);
	TestEqual(TEXT("Step 2 prompts the bar in at 12"), Fx.Director->GetPrompt().ToString(), FString(TEXT("Bar in at 12")));
	TestEqual(TEXT("Step 2's glyph is the bar"), Fx.Director->GetInputGlyph(), FName(TEXT("IA_Sheet")));
	TestEqual(TEXT("Step 2's cue is the timing ring"), Fx.Director->GetCue(), ELessonCue::TimingRing);

	float ProgressSeen = 0.0f;
	const bool bPassed = RideUntil(Fx, Rider, 150.0f, [&]
	{
		if (Fx.Director->GetPhase() == ELessonPhase::Step)
		{
			ProgressSeen = FMath::Max(ProgressSeen, Fx.Director->GetObjectiveProgress());
		}
		return Fx.Director->GetOutcome() == ELessonOutcome::Passed;
	});
	if (!TestTrue(TEXT("The scripted ride passes B2"), bPassed))
	{
		AddInfo(FString::Printf(TEXT("Stopped at step %d, outcome %d, %d failures, last fault '%s'"), Fx.Director->GetStepIndex() + 1,
			int32(Fx.Director->GetOutcome()), Fx.Director->GetStepFailures(), *Fx.Director->GetLastFaultLine().ToString()));
		return false;
	}
	TestEqual(TEXT("The director shows the result"), Fx.Director->GetPhase(), ELessonPhase::Result);
	TestTrue(TEXT("Progress climbed in fifths before the pass"), ProgressSeen >= 0.8f - KINDA_SMALL_NUMBER);
	TestEqual(TEXT("Objective progress is full at the pass"), Fx.Director->GetObjectiveProgress(), 1.0f);
	TestTrue(TEXT("The passing jump's height is the objective value, 1 to 2 m"), Fx.Director->GetObjectiveValue() >= 1.0f && Fx.Director->GetObjectiveValue() <= 2.0f);
	TestTrue(TEXT("At least five jumps were judged"), Fx.Director->GetLessonAttempts() >= 5);
	// Ridden with the lesson's own assists (the landing assist): one star, two with the higher bar (Stomped).
	TestEqual(TEXT("Stars: one, or two with the higher bar"), Fx.Director->GetStars(), Fx.Director->PassedHigherBar() ? 2 : 1);

	const FLessonRecord* Record = Fx.Lessons->GetProgress().Find(TEXT("B2"));
	TestTrue(TEXT("The pass is in the progress book: one attempt, one pass, the director's stars"),
		Record && Record->Attempts == 1 && Record->Passes == 1 && Record->BestStars == Fx.Director->GetStars() && !Record->bPassedNoAssists);
	TestTrue(TEXT("The best value is the passing jump's height"), Record && Record->bHasBestValue && FMath::IsNearlyEqual(Record->BestValue, Fx.Director->GetObjectiveValue(), 1e-3f));

	// Next: the first lesson after B2 that is unlocked, B3 (it needs B2).
	TestEqual(TEXT("Next goes to B3"), Fx.Director->GetNextLessonId(), FName(TEXT("B3")));
	TestTrue(TEXT("Next starts B3"), Fx.Director->Next());
	TestEqual(TEXT("B3 is running"), Fx.Director->GetLessonId(), FName(TEXT("B3")));
	TestEqual(TEXT("B3 starts at its intro"), Fx.Director->GetPhase(), ELessonPhase::Intro);
	TestEqual(TEXT("Stars are cleared for the new lesson"), Fx.Director->GetStars(), 0);
	return true;
}

// docs/tutorials.md S3: a ride that sheets in while the kite is still climbing fails B2, and the one
// fault line shown is the catalogue's for that mistake.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolDirectorDiagnosesBadSend, "KiteSurf.School.DirectorDiagnosesBadSend", SchoolDirectorTest::Flags)

bool FKiteSurfSchoolDirectorDiagnosesBadSend::RunTest(const FString& Parameters)
{
	FDirectorFixture Fx;
	const FLessonDef* B2 = LessonCatalog::Find(TEXT("B2"));
	if (!TestTrue(TEXT("Fixture and lesson B2"), Fx.IsValid() && B2))
	{
		return false;
	}
	const FLessonFault* Sheeted = B2->Faults.FindByPredicate([](const FLessonFault& F) { return F.Id == FName(TEXT("SheetedInClimbing")); });
	if (!TestNotNull(TEXT("B2 has the sheeted-in-while-climbing rule"), Sheeted))
	{
		return false;
	}
	Fx.Director->BeginLesson(*B2, Fx.Pawn);
	FJumpRider Rider = BadSendRider(Fx.Director);
	const bool bFailed = RideUntil(Fx, Rider, 120.0f, [&] { return Fx.Director->GetOutcome() == ELessonOutcome::AttemptFailed; });
	if (!TestTrue(TEXT("A jump after a bad send fails"), bFailed))
	{
		return false;
	}
	TestEqual(TEXT("It failed on step 2, the jumps"), Fx.Director->GetStepIndex(), 1);
	TestEqual(TEXT("The director shows an AttemptFailed result"), Fx.Director->GetPhase(), ELessonPhase::Result);
	TestEqual(TEXT("The fault is sheeting in while the kite climbs"), Fx.Director->GetLastFaultId(), FName(TEXT("SheetedInClimbing")));
	TestEqual(TEXT("The line is the catalogue's"), Fx.Director->GetLastFaultLine().ToString(), Sheeted->Feedback.ToString());
	TestEqual(TEXT("The line reads \"Bar out while it climbs, in at 12\""), Fx.Director->GetLastFaultLine().ToString(), FString(TEXT("Bar out while it climbs, in at 12")));
	TestEqual(TEXT("While it shows, the fault line is the prompt"), Fx.Director->GetPrompt().ToString(), Sheeted->Feedback.ToString());
	TestEqual(TEXT("One failure on the step"), Fx.Director->GetStepFailures(), 1);
	TestFalse(TEXT("No drop-back offer after one failure"), Fx.Director->IsDropBackOffered());
	TestEqual(TEXT("Nothing is recorded yet: the lesson goes on"), Fx.Lessons->GetProgress().Num(), 0);

	// The line shows for AttemptResultSeconds, then the step goes on with its prompt.
	const bool bBack = RideUntil(Fx, Rider, Fx.Director->AttemptResultSeconds + 0.5f, [&] { return Fx.Director->GetPhase() == ELessonPhase::Step; });
	TestTrue(TEXT("Back to the step after the line has shown"), bBack);
	TestEqual(TEXT("The step's prompt again"), Fx.Director->GetPrompt().ToString(), FString(TEXT("Bar in at 12")));

	// Leaving now counts the run as one failed attempt.
	Fx.Director->ExitToFreeRide();
	const FLessonRecord* Record = Fx.Lessons->GetProgress().Find(TEXT("B2"));
	TestTrue(TEXT("Exit records one failed attempt, no stars"), Record && Record->Attempts == 1 && Record->Passes == 0 && Record->BestStars == 0);
	return true;
}

// The set-up: the lesson's wind, kite, board, start and assists on the rider, a run's overrides, and
// everything put back on exit to free ride.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolDirectorAppliesSetup, "KiteSurf.School.DirectorAppliesSetup", SchoolDirectorTest::Flags)

bool FKiteSurfSchoolDirectorAppliesSetup::RunTest(const FString& Parameters)
{
	FDirectorFixture Fx;
	if (!TestTrue(TEXT("Fixture"), Fx.IsValid()))
	{
		return false;
	}
	UKiteComponent* Kite = Fx.Pawn->GetKite();
	UBoardMovementComponent* Board = Fx.Pawn->GetBoardMovement();
	UWindComponent* Wind = Fx.Pawn->GetWind();
	URiderAttitudeComponent* Attitude = Fx.Pawn->GetRiderAttitude();
	if (!TestTrue(TEXT("Rider components"), Kite && Board && Wind && Attitude))
	{
		return false;
	}
	// The free ride before the lesson: 20 kn, a Loop kite of 7 m2, the small board, auto-edge on, park off, landing assist at half.
	Kite->SetKiteModel(EKiteModel::Loop);
	Kite->SetKiteSize(7.0f);
	Board->SetBoardSize(EBoardSize::Small);
	Board->bAutoEdge = true;
	Kite->bParkHoldAssist = false;
	Attitude->AssistStrength = 0.5f;
	const float FreeRideSizeM2 = Kite->AreaM2;

	// A3: riding at 12 kn on tack +1, 12 kn wind, Boost at the recommended size, the large board, auto-park and auto-edge.
	const FLessonDef* A3 = LessonCatalog::Find(TEXT("A3"));
	Fx.Director->IntroSeconds = 3.0f;
	TestTrue(TEXT("A3 begins"), Fx.Director->BeginLesson(*A3, Fx.Pawn));
	TestNearlyEqual(TEXT("Wind is the lesson's 12 kn"), KiteUnits::CmSToKnots(Wind->BaseWind.Size()), 12.0f, 0.01f);
	TestTrue(TEXT("The wind keeps its direction (+X)"), Wind->BaseWind.GetSafeNormal().Equals(FVector::ForwardVector, 1e-4f));
	TestEqual(TEXT("Kite model is the lesson's Boost"), Kite->GetKiteModel(), EKiteModel::Boost);
	TestNearlyEqual(TEXT("Kite size is the one recommended for 12 kn (m2)"), Kite->AreaM2, UKiteComponent::RecommendKiteSizeM2(12.0f), 0.01f);
	TestEqual(TEXT("Board is the lesson's large one"), Board->GetBoardSize(), EBoardSize::Large);
	TestTrue(TEXT("Auto-park on"), Kite->bParkHoldAssist);
	TestTrue(TEXT("Auto-edge on"), Board->bAutoEdge);
	TestEqual(TEXT("Landing assist off (strength 0)"), Attitude->AssistStrength, 0.0f);
	TestEqual(TEXT("Riding start"), Fx.Director->GetAppliedStart(), ELessonStart::Riding);
	TestNearlyEqual(TEXT("Riding at the lesson's 12 kn"), KiteUnits::CmSToKnots(Board->Velocity.Size2D()), 12.0f, 0.05f);
	TestEqual(TEXT("Planing"), Board->GetBoardState(), EBoardState::Planing);
	const FVector Crosswind = FVector::CrossProduct(FVector::UpVector, Kite->GetDownwindDir().GetSafeNormal2D());
	TestTrue(TEXT("On tack +1: riding to the right looking downwind"), FVector::DotProduct(Board->Velocity, Crosswind) > 0.0f);
	TestEqual(TEXT("Intro first"), Fx.Director->GetPhase(), ELessonPhase::Intro);
	TestEqual(TEXT("The intro's prompt is the lesson's summary"), Fx.Director->GetPrompt().ToString(), A3->Summary.ToString());
	TestEqual(TEXT("No glyph in the intro"), Fx.Director->GetInputGlyph(), FName());
	for (int32 I = 0; I < 30; ++I)
	{
		Fx.Frame();
	}
	TestEqual(TEXT("Still in the intro after 0.5 s"), Fx.Director->GetPhase(), ELessonPhase::Intro);
	for (int32 I = 0; I < 160; ++I)
	{
		Fx.Frame();
	}
	TestEqual(TEXT("The first step after the 3 s intro"), Fx.Director->GetPhase(), ELessonPhase::Step);
	TestEqual(TEXT("Step 1's prompt"), Fx.Director->GetPrompt().ToString(), A3->Steps[0].Prompt.ToString());
	TestEqual(TEXT("Step 1's glyph"), Fx.Director->GetInputGlyph(), A3->Steps[0].InputGlyph);
	TestEqual(TEXT("Step 1's cue"), Fx.Director->GetCue(), ELessonCue::WindowArc);
	// 190 frames at 60 fps with the pawn's 240 Hz steps: one sample per four steps, every frame.
	TestTrue(TEXT("Telemetry at 60 Hz: one sample a frame"), FMath::Abs(Fx.Director->GetTelemetry().Num() - 190) <= 2);
	const FLessonSample& Latest = Fx.Director->GetTelemetry().Latest();
	TestNearlyEqual(TEXT("The sample is on the board's clock"), Latest.TimeSeconds, Board->GetSimTimeSeconds(), 1e-4f);
	TestNearlyEqual(TEXT("The sample's kite elevation is the kite's (deg)"), Latest.KiteElevationDeg, Kite->GetElevationDeg(), 1e-3f);
	TestNearlyEqual(TEXT("The sample's speed is the board's (m/s)"), Latest.SpeedMS, KiteUnits::CmToM(Board->Velocity.Size2D()), 1e-3f);
	TestEqual(TEXT("The sample's tack is +1"), Latest.Tack, 1);

	// A1: a floating start (kite at 12), and a run with more wind and every assist off.
	const FLessonDef* A1 = LessonCatalog::Find(TEXT("A1"));
	FLessonRunOptions Options;
	Options.WindKnots = 16.0f;
	Options.bOverrideAssists = true;
	TestTrue(TEXT("A1 begins with overrides"), Fx.Director->BeginLesson(*A1, Fx.Pawn, Options));
	TestNearlyEqual(TEXT("The run's 16 kn"), KiteUnits::CmSToKnots(Wind->BaseWind.Size()), 16.0f, 0.01f);
	TestNearlyEqual(TEXT("Kite size recommended for the run's wind (m2)"), Kite->AreaM2, UKiteComponent::RecommendKiteSizeM2(16.0f), 0.01f);
	TestFalse(TEXT("Auto-park off by the override"), Kite->bParkHoldAssist);
	TestFalse(TEXT("Auto-edge off by the override"), Board->bAutoEdge);
	TestEqual(TEXT("No assists used"), Fx.Director->GetUsedAssists().CountOn(), 0);
	TestEqual(TEXT("Floating start"), Fx.Director->GetAppliedStart(), ELessonStart::Floating);
	TestEqual(TEXT("Not moving"), Board->Velocity.Size2D(), 0.0);
	TestNotEqual(TEXT("Not planing"), Board->GetBoardState(), EBoardState::Planing);
	TestTrue(TEXT("Kite at 12 (clock within 5 deg)"), FMath::Abs(Kite->GetClockDeg()) < 5.0f);
	FLessonRunOptions Lower;
	Lower.WindKnots = 6.0f;
	Fx.Director->BeginLesson(*A1, Fx.Pawn, Lower);
	TestNearlyEqual(TEXT("A run cannot lower the lesson's wind"), KiteUnits::CmSToKnots(Wind->BaseWind.Size()), A1->Setup.WindKnots, 0.01f);
	TestTrue(TEXT("Without the override the lesson's assists: A1 auto-park"), Kite->bParkHoldAssist);
	TestEqual(TEXT("Unsupported starts: standing floats, airborne rides"),
		FString::Printf(TEXT("%d %d"), int32(ALessonDirector::SupportedStart(ELessonStart::Standing)), int32(ALessonDirector::SupportedStart(ELessonStart::Airborne))),
		FString::Printf(TEXT("%d %d"), int32(ELessonStart::Floating), int32(ELessonStart::Riding)));

	// Exit: the free ride's wind, gear and assists are back.
	Fx.Director->ExitToFreeRide();
	TestEqual(TEXT("Idle after exit"), Fx.Director->GetPhase(), ELessonPhase::Idle);
	TestNearlyEqual(TEXT("Free-ride wind back (20 kn)"), KiteUnits::CmSToKnots(Wind->BaseWind.Size()), 20.0f, 0.01f);
	TestEqual(TEXT("Free-ride kite model back"), Kite->GetKiteModel(), EKiteModel::Loop);
	TestNearlyEqual(TEXT("Free-ride kite size back (m2)"), Kite->AreaM2, FreeRideSizeM2, 0.01f);
	TestEqual(TEXT("Free-ride board back"), Board->GetBoardSize(), EBoardSize::Small);
	TestTrue(TEXT("Auto-edge back on"), Board->bAutoEdge);
	TestFalse(TEXT("Park back off"), Kite->bParkHoldAssist);
	TestEqual(TEXT("Landing assist strength back"), Attitude->AssistStrength, 0.5f);
	TestEqual(TEXT("Nothing was judged, so nothing recorded"), Fx.Lessons->GetProgress().Num(), 0);
	return true;
}

// After three failed attempts on one step the director offers to drop back, and taking the offer
// goes back a step.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolDirectorDropBackOffer, "KiteSurf.School.DirectorDropBackOffer", SchoolDirectorTest::Flags)

bool FKiteSurfSchoolDirectorDropBackOffer::RunTest(const FString& Parameters)
{
	FDirectorFixture Fx;
	const FLessonDef* B2 = LessonCatalog::Find(TEXT("B2"));
	if (!TestTrue(TEXT("Fixture and lesson B2"), Fx.IsValid() && B2))
	{
		return false;
	}
	Fx.Director->BeginLesson(*B2, Fx.Pawn);
	FJumpRider Rider = BadSendRider(Fx.Director);
	int32 OfferedAtFailures = -1;
	bool bOfferedEarly = false;
	const bool bOffered = RideUntil(Fx, Rider, 200.0f, [&]
	{
		if (Fx.Director->IsDropBackOffered())
		{
			OfferedAtFailures = Fx.Director->GetStepFailures();
			return true;
		}
		bOfferedEarly |= Fx.Director->GetStepFailures() >= Fx.Director->DropBackAfterFailures;
		return false;
	});
	if (!TestTrue(TEXT("The drop-back offer comes"), bOffered))
	{
		AddInfo(FString::Printf(TEXT("%d failures on step %d"), Fx.Director->GetStepFailures(), Fx.Director->GetStepIndex() + 1));
		return false;
	}
	TestEqual(TEXT("Offered at the third failure, not before"), OfferedAtFailures, 3);
	TestFalse(TEXT("Never three failures without the offer"), bOfferedEarly);
	TestEqual(TEXT("On step 2"), Fx.Director->GetStepIndex(), 1);
	TestEqual(TEXT("On step 2 the drop back is to step 1; the first step's would be B1"), Fx.Director->GetDropBackLessonId(), FName(TEXT("B1")));

	TestTrue(TEXT("Taking the offer"), Fx.Director->AcceptDropBack());
	TestEqual(TEXT("Back on step 1"), Fx.Director->GetStepIndex(), 0);
	TestEqual(TEXT("In the step"), Fx.Director->GetPhase(), ELessonPhase::Step);
	TestFalse(TEXT("The offer is cleared"), Fx.Director->IsDropBackOffered());
	TestEqual(TEXT("The failures are cleared"), Fx.Director->GetStepFailures(), 0);
	TestEqual(TEXT("Step 1's prompt"), Fx.Director->GetPrompt().ToString(), B2->Steps[0].Prompt.ToString());
	TestFalse(TEXT("No offer, nothing to take"), Fx.Director->AcceptDropBack());

	// Retry starts the lesson again from its set-up and counts the run so far as a failed attempt.
	TestTrue(TEXT("Retry"), Fx.Director->Retry());
	TestEqual(TEXT("Retry goes back to the intro (0 s here) on step 1"), Fx.Director->GetStepIndex(), 0);
	const FLessonRecord* Record = Fx.Lessons->GetProgress().Find(TEXT("B2"));
	TestTrue(TEXT("The abandoned run is one failed attempt"), Record && Record->Attempts == 1 && Record->Passes == 0);
	return true;
}

// ULessonSubsystem::StartLesson: locked, unknown and unbuilt lessons are refused; an unlocked one is
// pending until a ride starts it, and starting it spawns a director in the rider's world.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolStartLessonRespectsUnlock, "KiteSurf.School.StartLessonRespectsUnlock", SchoolDirectorTest::Flags)

bool FKiteSurfSchoolStartLessonRespectsUnlock::RunTest(const FString& Parameters)
{
	ULessonSubsystem* Lessons = NewObject<ULessonSubsystem>(NewObject<UKiteSurfGameInstance>());
	Lessons->SetWriteToDisk(false);
	Lessons->SetTravelEnabled(false);

	TestFalse(TEXT("B2 is locked at the start"), Lessons->StartLesson(TEXT("B2")));
	TestFalse(TEXT("An unknown lesson is refused"), Lessons->StartLesson(TEXT("Z9")));
	TestEqual(TEXT("Nothing pending after refusals"), Lessons->GetPendingLessonId(), FName());
	TestTrue(TEXT("A1 is unlocked and starts"), Lessons->StartLesson(TEXT("A1")));
	TestEqual(TEXT("A1 is pending (no world to run it in)"), Lessons->GetPendingLessonId(), FName(TEXT("A1")));

	for (const TCHAR* Id : { TEXT("A1"), TEXT("A2"), TEXT("A3"), TEXT("A4"), TEXT("A5") })
	{
		Lessons->RecordLessonResult(Id, true, 1, NAN, false);
	}
	TestFalse(TEXT("A6 stays refused with its prerequisite passed: toeside riding is not built"), Lessons->StartLesson(TEXT("A6")));
	TestEqual(TEXT("A refusal leaves the pending lesson alone"), Lessons->GetPendingLessonId(), FName(TEXT("A1")));
	TestTrue(TEXT("B1 is unlocked after A4"), Lessons->StartLesson(TEXT("B1")));
	TestEqual(TEXT("B1 replaces A1 as pending"), Lessons->GetPendingLessonId(), FName(TEXT("B1")));
	TestFalse(TEXT("B3 is still locked"), Lessons->StartLesson(TEXT("B3")));
	TestTrue(TEXT("The console's force skips the prerequisites"), Lessons->StartLessonIgnoringPrerequisites(TEXT("B3")));
	TestFalse(TEXT("...but not an unbuilt feature (B4 needs grabs)"), Lessons->StartLessonIgnoringPrerequisites(TEXT("B4")));
	TestEqual(TEXT("B3 is pending"), Lessons->GetPendingLessonId(), FName(TEXT("B3")));

	// The ride level starts the pending lesson on the rider (what AKiteSurfGameMode does once the rider spawns).
	FDirectorFixture Fx;
	if (!TestTrue(TEXT("Fixture"), Fx.IsValid()))
	{
		return false;
	}
	Fx.Pawn->GetBoardMovement()->bAutoEdge = true;
	ALessonDirector* Director = Lessons->StartPendingLesson(Fx.Pawn);
	if (!TestNotNull(TEXT("A director is spawned for the pending lesson"), Director))
	{
		return false;
	}
	TestEqual(TEXT("In the rider's world"), Director->GetWorld(), Fx.World);
	TestEqual(TEXT("Running B3"), Director->GetLessonId(), FName(TEXT("B3")));
	TestEqual(TEXT("On the rider"), Director->GetRider(), Fx.Pawn);
	TestEqual(TEXT("The pending lesson is consumed"), Lessons->GetPendingLessonId(), FName());
	TestNull(TEXT("Nothing pending: nothing to start"), Lessons->StartPendingLesson(Fx.Pawn));
	TestFalse(TEXT("B3's assists: no auto-edge"), Fx.Pawn->GetBoardMovement()->bAutoEdge);

	// One lesson at a time: a new one ends the running director and puts the free ride back first.
	Lessons->StartLessonIgnoringPrerequisites(TEXT("A3"));
	ALessonDirector* Second = Lessons->StartPendingLesson(Fx.Pawn);
	TestTrue(TEXT("A second lesson gets its own director"), Second && Second != Director);
	TestTrue(TEXT("The first director is gone"), !IsValid(Director));
	TestTrue(TEXT("A3 runs, with its auto-edge"), Second && Second->GetLessonId() == FName(TEXT("A3")) && Fx.Pawn->GetBoardMovement()->bAutoEdge);
	if (Second)
	{
		Second->ExitToFreeRide();
	}
	TestTrue(TEXT("The free ride's auto-edge is back after exit"), Fx.Pawn->GetBoardMovement()->bAutoEdge);
	return true;
}

#endif
