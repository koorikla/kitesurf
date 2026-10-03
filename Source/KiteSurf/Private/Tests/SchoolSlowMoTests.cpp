#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "BoardMovementComponent.h"
#include "KiteComponent.h"
#include "KiteRiderPawn.h"
#include "KiteSurfUnits.h"
#include "WindComponent.h"
#include "School/LessonCatalog.h"
#include "School/LessonDirector.h"
#include "School/LessonHUD.h"
#include "School/LessonSubsystem.h"
#include "School/LessonTiming.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/TrickTrackerComponent.h"
#include "UI/KiteSurfGameInstance.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the kite school's slow motion at a step's decision point (docs/tutorials.md 3.4 and S8):
// the schedule and the cue detection as pure functions, and an ALessonDirector on a spawned rider
// whose frames are scaled by the world's time dilation the way UWorld::Tick scales them (real frame
// time 1/60 s times the effective dilation), so the slow motion the director writes reaches the
// pawn's fixed 240 Hz step loop as it does in the game.
namespace SchoolSlowMoTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	/** A 60 Hz display: real seconds per frame. */
	const float RealFrameSeconds = 1.0f / 60.0f;

	/**
	 * A scripted rider for the jump lessons, as SchoolDirectorTests.cpp's: ride with the kite low,
	 * send it to 12 with the bar part out and the edge loaded, pop once the kite has been over
	 * PopElevationDeg for a moment, then ride again. With bHandsStillInAir the hands do not move from
	 * the pop to the touchdown (no steering, no crouch): the inputs over the whole flight are the same
	 * in every run, whatever the frames.
	 */
	struct FSendRider
	{
		enum class EPhase { Ride, Send, Air };
		EPhase Phase = EPhase::Ride;
		float PhaseSeconds = 0.0f;
		float AirSeconds = 0.0f;
		float AtTopSeconds = 0.0f;
		float RideSeconds = 5.0f;
		float RideClockDeg = 65.0f;
		float SendBar = 0.6f;
		float RideBar = 0.7f;
		float Edge = 0.8f;
		float SteerDampingSeconds = 0.6f;
		float LastClock = 0.0f;
		bool bHasLastClock = false;
		float PopBar = 0.8f;
		float PopHoldSeconds = 0.1f;
		float MaxSendSeconds = 6.0f;
		float PopElevationDeg = 80.0f;
		/** Or pop once the kite has stopped climbing near the top for this long (s). */
		float StalledPopSeconds = 0.4f;
		float StalledSeconds = 0.0f;
		float LastElevation = 0.0f;
		bool bHasLastElevation = false;
		bool bHandsStillInAir = false;
		int32 Pops = 0;
		/** While this director is on its first step ("send the kite to 12"), hold the send until it moves on. */
		const ALessonDirector* WaitOnFirstStep = nullptr;

		void Step(AKiteRiderPawn* Pawn, float Dt)
		{
			UKiteComponent* Kite = Pawn->GetKite();
			UBoardMovementComponent* Board = Pawn->GetBoardMovement();
			const float Clock = Kite->GetClockDeg();
			const float ClockRate = bHasLastClock && Dt > 0.0f ? (Clock - LastClock) / Dt : 0.0f;
			LastClock = Clock;
			bHasLastClock = true;
			auto SteerTo = [&](float TargetClock, float GainDeg, float MaxSteer)
			{
				Pawn->SteerKite(FMath::Clamp((TargetClock - Clock - SteerDampingSeconds * ClockRate) / GainDeg, -MaxSteer, MaxSteer));
			};
			PhaseSeconds += Dt;
			const bool bAir = Board->GetBoardState() == EBoardState::Airborne;
			switch (Phase)
			{
			case EPhase::Ride:
				SteerTo(RideClockDeg, 30.0f, 0.6f);
				Pawn->SheetKite(RideBar);
				Pawn->EdgeBoard(Edge);
				if (PhaseSeconds >= RideSeconds && !bAir && !Board->IsCrashing())
				{
					Phase = EPhase::Send;
					PhaseSeconds = 0.0f;
					AtTopSeconds = 0.0f;
					StalledSeconds = 0.0f;
					bHasLastElevation = false;
				}
				break;
			case EPhase::Send:
				SteerTo(0.0f, 25.0f, 0.8f);
				Pawn->SheetKite(SendBar);
				Pawn->EdgeBoard(Edge);
				Board->SetWeightShift(-1.0f);
				Pawn->SetLoadHeld(true);
				AtTopSeconds = Kite->GetElevationDeg() >= PopElevationDeg ? AtTopSeconds + Dt : 0.0f;
				// Loaded, the kite tops out lower (about 74 deg in 14 kn): it stopped climbing over 70 deg.
				{
					const float Elevation = Kite->GetElevationDeg();
					const float ClimbRate = bHasLastElevation && Dt > 0.0f ? (Elevation - LastElevation) / Dt : 99.0f;
					StalledSeconds = Elevation >= 70.0f && ClimbRate <= 2.0f ? StalledSeconds + Dt : 0.0f;
					LastElevation = Elevation;
					bHasLastElevation = true;
				}
				if (WaitOnFirstStep && WaitOnFirstStep->GetPhase() == ELessonPhase::Step && WaitOnFirstStep->GetStepIndex() == 0)
				{
					Pawn->SetLoadHeld(false);
					Board->SetWeightShift(0.0f);
					break;
				}
				if (AtTopSeconds >= PopHoldSeconds || StalledSeconds >= StalledPopSeconds || PhaseSeconds > MaxSendSeconds)
				{
					Pawn->SheetKite(PopBar);
					Pawn->ReleaseLoadAndPop();
					Board->SetWeightShift(0.0f);
					if (bHandsStillInAir)
					{
						Pawn->SteerKite(0.0f);
					}
					Phase = EPhase::Air;
					PhaseSeconds = 0.0f;
					AirSeconds = 0.0f;
					++Pops;
				}
				break;
			case EPhase::Air:
				if (!bHandsStillInAir)
				{
					SteerTo(0.0f, 40.0f, 0.3f);
				}
				if (bAir)
				{
					AirSeconds += Dt;
					if (!bHandsStillInAir && Board->Velocity.Z < 0.0f)
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

	/** A pawn and a lesson director in a throwaway world, stepped at a 60 Hz display with the world's time dilation applied. */
	struct FSlowMoFixture
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		ALessonDirector* Director = nullptr;
		ULessonSubsystem* Lessons = nullptr;
		/** Real seconds run, and frames. */
		float RealSeconds = 0.0f;
		int32 Frames = 0;

		FSlowMoFixture()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (World && GEngine)
			{
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

		~FSlowMoFixture()
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

		bool IsValid() const { return Pawn && Director && Lessons && World->GetWorldSettings(); }

		/** The world's time dilation now: what the next frame is scaled by. */
		float WorldDilation() const { return World->GetWorldSettings()->GetEffectiveTimeDilation(); }

		/** One display frame: game time is real time times the world's dilation, as UWorld::Tick makes it. Returns the game seconds. */
		float Frame(FSendRider* Rider)
		{
			const float Dt = RealFrameSeconds * WorldDilation();
			if (Rider)
			{
				Rider->Step(Pawn, Dt);
			}
			Pawn->Tick(Dt);
			Director->UpdateLesson(Dt);
			RealSeconds += RealFrameSeconds;
			++Frames;
			return Dt;
		}

		/** Frames until Done says so or RealLimit real seconds pass; false on timeout. */
		template <typename FDone>
		bool RunUntil(FSendRider* Rider, float RealLimit, FDone&& Done)
		{
			for (float T = 0.0f; T < RealLimit; T += RealFrameSeconds)
			{
				Frame(Rider);
				if (Done())
				{
					return true;
				}
			}
			return false;
		}
	};

	FLessonSample SlowMoSample(float Time, EBoardState State, float KiteDeg, float Bar, float HeightM = 0.0f, float VerticalMS = 0.0f)
	{
		FLessonSample S;
		S.TimeSeconds = Time;
		S.BoardState = State;
		S.KiteElevationDeg = KiteDeg;
		S.BarPosition = Bar;
		S.HeightM = HeightM;
		S.VerticalSpeedMS = VerticalMS;
		return S;
	}
}

using namespace SchoolSlowMoTest;

// The schedule (0.6x for about 0.6 s, eased in and out, in real seconds), the two decision points as
// pure functions over telemetry samples, and where the catalogue puts them.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolSlowMoCues, "KiteSurf.School.SlowMoCues", SchoolSlowMoTest::Flags)

bool FKiteSurfSchoolSlowMoCues::RunTest(const FString& Parameters)
{
	using namespace LessonTiming;
	// Schedule.
	TestEqual(TEXT("Real time before it starts"), SlowMoDilationAt(0.0f), 1.0f);
	TestEqual(TEXT("Real time after it ends"), SlowMoDilationAt(SlowMoTotalSeconds() + 0.01f), 1.0f);
	TestNearlyEqual(TEXT("Halfway down the ramp in: halfway to 0.6x"), SlowMoDilationAt(0.5f * SlowMoRampInSeconds), 0.8f, 1e-4f);
	TestNearlyEqual(TEXT("Held at 0.6x"), SlowMoDilationAt(SlowMoRampInSeconds + 0.5f * SlowMoHoldSeconds), SlowMoDilation, 1e-6f);
	TestNearlyEqual(TEXT("Halfway up the ramp out"), SlowMoDilationAt(SlowMoReleaseSeconds() + 0.5f * SlowMoRampOutSeconds), 0.8f, 1e-4f);
	TestNearlyEqual(TEXT("The whole slow motion is about 0.8 real s"), SlowMoTotalSeconds(), 0.8f, 1e-4f);
	float Prev = 1.0f;
	bool bDownMonotonic = true;
	bool bUpMonotonic = true;
	float LargestStep = 0.0f;
	for (float T = 0.0f; T <= SlowMoTotalSeconds() + 0.05f; T += 1.0f / 240.0f)
	{
		const float D = SlowMoDilationAt(T);
		bDownMonotonic &= T > SlowMoReleaseSeconds() || D <= Prev + 1e-6f;
		bUpMonotonic &= T <= SlowMoReleaseSeconds() || D >= Prev - 1e-6f;
		LargestStep = FMath::Max(LargestStep, FMath::Abs(D - Prev));
		TestTrue(TEXT("Never below 0.6x"), D >= SlowMoDilation - 1e-6f);
		Prev = D;
	}
	TestTrue(TEXT("Eases down without going back up"), bDownMonotonic);
	TestTrue(TEXT("Eases back up without dipping"), bUpMonotonic);
	TestTrue(FString::Printf(TEXT("Smooth: no step over 0.03 per 1/240 s (largest %.4f)"), LargestStep), LargestStep < 0.03f);

	// The kite at the top: on the water, bar not in yet, once per send.
	FLessonSlowMoCue AtTop;
	AtTop.Trigger = ELessonSlowMoTrigger::KiteAtTop;
	AtTop.Threshold = 80.0f;
	{
		FSlowMoArm Arm;
		TestFalse(TEXT("Kite climbing at 70 deg: not yet"), DetectSlowMoCue(AtTop, SlowMoSample(0.0f, EBoardState::Planing, 70.0f, 0.5f), Arm));
		TestFalse(TEXT("At 80 deg with the bar already in: too late for the cue"), DetectSlowMoCue(AtTop, SlowMoSample(0.1f, EBoardState::Planing, 82.0f, 0.9f), Arm));
		TestTrue(TEXT("At 80 deg, on the water, bar out: the decision point"), DetectSlowMoCue(AtTop, SlowMoSample(0.2f, EBoardState::Planing, 81.0f, 0.5f), Arm));
		TestFalse(TEXT("Still at the top: once per send"), DetectSlowMoCue(AtTop, SlowMoSample(0.3f, EBoardState::Planing, 85.0f, 0.5f), Arm));
		TestFalse(TEXT("Back down to 70 deg is not low enough to arm again"), DetectSlowMoCue(AtTop, SlowMoSample(0.4f, EBoardState::Planing, 70.0f, 0.5f), Arm)
			|| DetectSlowMoCue(AtTop, SlowMoSample(0.5f, EBoardState::Planing, 81.0f, 0.5f), Arm));
		DetectSlowMoCue(AtTop, SlowMoSample(0.6f, EBoardState::Planing, 40.0f, 0.5f), Arm);
		TestTrue(TEXT("After the kite was low again, the next send fires it"), DetectSlowMoCue(AtTop, SlowMoSample(0.7f, EBoardState::Planing, 80.0f, 0.5f), Arm));
		// A loaded send that tops out under 80 deg: the top of the climb is the decision point.
		FSlowMoArm Loaded;
		TestFalse(TEXT("Climbing through 60 deg"), DetectSlowMoCue(AtTop, SlowMoSample(0.0f, EBoardState::Planing, 60.0f, 0.5f), Loaded));
		TestFalse(TEXT("Climbing through 70 deg at 10 deg/s"), DetectSlowMoCue(AtTop, SlowMoSample(1.0f, EBoardState::Planing, 70.0f, 0.5f), Loaded));
		TestFalse(TEXT("Still climbing at 73 deg"), DetectSlowMoCue(AtTop, SlowMoSample(1.5f, EBoardState::Planing, 73.0f, 0.5f), Loaded));
		TestTrue(TEXT("Stopped climbing at 74 deg (under 2 deg/s): the top"), DetectSlowMoCue(AtTop, SlowMoSample(2.0f, EBoardState::Planing, 73.8f, 0.5f), Loaded));
		FSlowMoArm Low;
		DetectSlowMoCue(AtTop, SlowMoSample(0.0f, EBoardState::Planing, 66.0f, 0.5f), Low);
		TestFalse(TEXT("Stopped at 66 deg: too far under the top"), DetectSlowMoCue(AtTop, SlowMoSample(1.0f, EBoardState::Planing, 66.0f, 0.5f), Low));

		FSlowMoArm Fresh;
		TestFalse(TEXT("Not in the air"), DetectSlowMoCue(AtTop, SlowMoSample(0.0f, EBoardState::Airborne, 85.0f, 0.5f), Fresh));
		FLessonSample Fallen = SlowMoSample(0.0f, EBoardState::Displacement, 85.0f, 0.5f);
		Fallen.bFallen = true;
		TestFalse(TEXT("Not with the rider fallen"), DetectSlowMoCue(AtTop, Fallen, Fresh));
	}

	// The rider on the way down: at or under 4 m, once per jump, only on a real jump.
	FLessonSlowMoCue Descending;
	Descending.Trigger = ELessonSlowMoTrigger::RiderDescending;
	Descending.Threshold = 4.0f;
	{
		FSlowMoArm Arm;
		TestFalse(TEXT("On the water"), DetectSlowMoCue(Descending, SlowMoSample(0.0f, EBoardState::Planing, 85.0f, 0.5f), Arm));
		TestFalse(TEXT("Climbing through 3 m"), DetectSlowMoCue(Descending, SlowMoSample(0.1f, EBoardState::Airborne, 85.0f, 0.9f, 3.0f, 4.0f), Arm));
		TestFalse(TEXT("Apex at 6 m"), DetectSlowMoCue(Descending, SlowMoSample(0.6f, EBoardState::Airborne, 85.0f, 0.9f, 6.0f, 0.0f), Arm));
		TestFalse(TEXT("Coming down at 5 m: not yet"), DetectSlowMoCue(Descending, SlowMoSample(0.9f, EBoardState::Airborne, 85.0f, 0.9f, 5.0f, -4.0f), Arm));
		TestTrue(TEXT("Down through 4 m: the dive"), DetectSlowMoCue(Descending, SlowMoSample(1.0f, EBoardState::Airborne, 85.0f, 0.9f, 3.9f, -5.0f), Arm));
		TestFalse(TEXT("Once per jump"), DetectSlowMoCue(Descending, SlowMoSample(1.1f, EBoardState::Airborne, 85.0f, 0.9f, 3.0f, -6.0f), Arm));
		DetectSlowMoCue(Descending, SlowMoSample(1.5f, EBoardState::Planing, 60.0f, 0.5f), Arm);
		TestFalse(TEXT("A 1.5 m jump climbing"), DetectSlowMoCue(Descending, SlowMoSample(2.0f, EBoardState::Airborne, 85.0f, 0.9f, 1.5f, 0.5f), Arm));
		TestTrue(TEXT("...fires just after its apex: it never reached 4 m"), DetectSlowMoCue(Descending, SlowMoSample(2.1f, EBoardState::Airborne, 85.0f, 0.9f, 1.45f, -0.3f), Arm));
		DetectSlowMoCue(Descending, SlowMoSample(2.5f, EBoardState::Planing, 60.0f, 0.5f), Arm);
		TestFalse(TEXT("A 0.3 m hop is not a jump"), DetectSlowMoCue(Descending, SlowMoSample(3.0f, EBoardState::Airborne, 85.0f, 0.9f, 0.3f, 0.0f), Arm)
			|| DetectSlowMoCue(Descending, SlowMoSample(3.1f, EBoardState::Airborne, 85.0f, 0.9f, 0.25f, -1.0f), Arm));
	}

	// The catalogue: B2's sheet-in step at 80 deg and both B3 steps at 4 m, each lesson with the
	// slow-motion assist on; nothing else.
	for (const FLessonDef& L : LessonCatalog::GetAll())
	{
		for (int32 I = 0; I < L.Steps.Num(); ++I)
		{
			const FLessonSlowMoCue& Cue = L.Steps[I].SlowMo;
			const FString Where = FString::Printf(TEXT("%s step %d"), *L.Id.ToString(), I + 1);
			const bool bExpected = (L.Id == FName(TEXT("B2")) && I == 1) || L.Id == FName(TEXT("B3"));
			TestEqual(*FString::Printf(TEXT("%s: slow-motion cue %s"), *Where, bExpected ? TEXT("set") : TEXT("unset")), Cue.IsSet(), bExpected);
			if (Cue.IsSet())
			{
				TestTrue(*FString::Printf(TEXT("%s: the lesson rides with the slow-motion assist"), *Where), L.Setup.Assists.bSlowMotion);
				TestFalse(*FString::Printf(TEXT("%s: a prompt"), *Where), Cue.Prompt.IsEmpty());
			}
		}
	}
	const FLessonDef* B2 = LessonCatalog::Find(TEXT("B2"));
	const FLessonDef* B3 = LessonCatalog::Find(TEXT("B3"));
	if (TestTrue(TEXT("B2 and B3"), B2 && B3))
	{
		TestEqual(TEXT("B2: the kite at the top"), B2->Steps[1].SlowMo.Trigger, ELessonSlowMoTrigger::KiteAtTop);
		TestEqual(TEXT("B2: at 80 deg"), B2->Steps[1].SlowMo.Threshold, 80.0f);
		TestEqual(TEXT("B2: \"Bar in now\""), B2->Steps[1].SlowMo.Prompt.ToString(), FString(TEXT("Bar in now")));
		TestEqual(TEXT("B3: the rider coming down"), B3->Steps[0].SlowMo.Trigger, ELessonSlowMoTrigger::RiderDescending);
		TestEqual(TEXT("B3: at 4 m"), B3->Steps[0].SlowMo.Threshold, 4.0f);
		TestEqual(TEXT("B3: \"Dive the kite now\""), B3->Steps[0].SlowMo.Prompt.ToString(), FString(TEXT("Dive the kite now")));
	}
	return true;
}

// The HUD lesson layer during a slow motion: its one prompt replaces the step's, drawn larger, with
// the step's glyph and no hint.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfHUDLessonSlowMoPrompt, "KiteSurf.HUD.LessonSlowMoPrompt", SchoolSlowMoTest::Flags)

bool FKiteSurfHUDLessonSlowMoPrompt::RunTest(const FString& Parameters)
{
	const FLessonDef* B2 = LessonCatalog::Find(TEXT("B2"));
	if (!TestNotNull(TEXT("B2"), B2))
	{
		return false;
	}
	FLessonHUDInput In;
	In.Phase = ELessonPhase::Step;
	In.LessonId = B2->Id;
	In.Title = B2->Title;
	In.StepIndex = 1;
	In.StepCount = B2->Steps.Num();
	In.StepPrompt = B2->Steps[1].Prompt;
	In.Glyph = B2->Steps[1].InputGlyph;
	In.Cue = B2->Steps[1].Cue;
	In.bHasObjective = true;
	In.Objective = B2->Pass;
	In.HeldHint = TEXT("A hint");
	FLessonHUDTimers Timers;
	Timers.bShowHint = true;
	const FLessonHUDView Normal = LessonHUD::BuildView(In, Timers);
	TestEqual(TEXT("Without slow motion: the step's prompt"), Normal.Prompt, FString(TEXT("Bar in at 12")));
	TestFalse(TEXT("...drawn as usual"), Normal.bSlowMotion);

	In.bSlowMotion = true;
	In.SlowMotionPrompt = B2->Steps[1].SlowMo.Prompt;
	const FLessonHUDView Slow = LessonHUD::BuildView(In, Timers);
	TestEqual(TEXT("Slow motion: its one prompt"), Slow.Prompt, FString(TEXT("Bar in now")));
	TestTrue(TEXT("...drawn as the slow-motion prompt"), Slow.bSlowMotion);
	TestEqual(TEXT("...with the bar's glyph"), Slow.Glyph, LessonHUD::GlyphText(TEXT("IA_Sheet")));
	TestEqual(TEXT("...and nothing else to read"), Slow.Hint, FString());
	TestEqual(TEXT("The prompt is the only line besides the header, glyph and progress"), Slow.PanelLines().Num(), Normal.PanelLines().Num() - 1);
	return true;
}

// B2 on a rider: slow motion starts at the decision point (the kite reaching 80 deg as the bar-in
// step starts), writes the world's time dilation down to 0.6x and back on its real-time schedule,
// shows its prompt through the HUD layer, and leaves the world at exactly 1 afterwards.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolSlowMoEngagesAndReleases, "KiteSurf.School.SlowMoEngagesAndReleases", SchoolSlowMoTest::Flags)

bool FKiteSurfSchoolSlowMoEngagesAndReleases::RunTest(const FString& Parameters)
{
	FSlowMoFixture Fx;
	const FLessonDef* B2 = LessonCatalog::Find(TEXT("B2"));
	if (!TestTrue(TEXT("Fixture and lesson B2"), Fx.IsValid() && B2))
	{
		return false;
	}
	TestTrue(TEXT("B2 begins"), Fx.Director->BeginLesson(*B2, Fx.Pawn));
	FSendRider Rider;
	Rider.WaitOnFirstStep = Fx.Director;
	bool bSlowOnStep1 = false;
	const bool bStep2 = Fx.RunUntil(&Rider, 40.0f, [&]
	{
		bSlowOnStep1 |= Fx.Director->GetStepIndex() == 0 && (Fx.Director->IsSlowMotionActive() || Fx.WorldDilation() != 1.0f);
		return Fx.Director->GetStepIndex() == 1;
	});
	if (!TestTrue(TEXT("Step 2 (bar in at 12) comes"), bStep2))
	{
		return false;
	}
	TestFalse(TEXT("No slow motion on step 1: it has no decision point"), bSlowOnStep1);
	TestTrue(TEXT("Step 2's slow motion is on"), Fx.Director->IsSlowMotionOnForStep());
	TestTrue(TEXT("Slow motion starts with step 2: the kite is at the top and the bar not in yet"), Fx.Director->IsSlowMotionActive());
	TestEqual(TEXT("One slow motion so far"), Fx.Director->GetSlowMotionSerial(), 1);
	TestTrue(TEXT("...with the kite at 80 deg or more"), Fx.Pawn->GetKite()->GetElevationDeg() >= 80.0f);
	TestEqual(TEXT("Its prompt"), Fx.Director->GetSlowMotionPrompt().ToString(), FString(TEXT("Bar in now")));
	FLessonHUDLayer Layer;
	Layer.Update(Fx.Director, RealFrameSeconds);
	TestEqual(TEXT("The HUD layer shows the one prompt"), Layer.GetView().Prompt, FString(TEXT("Bar in now")));
	TestTrue(TEXT("...as the slow-motion prompt"), Layer.GetView().bSlowMotion);

	// Follow it to the end in real time, frame by frame.
	const float BoardStart = Fx.Pawn->GetBoardMovement()->GetSimTimeSeconds();
	float RealSlow = 0.0f;
	float MinDilation = 1.0f;
	float LargestChange = 0.0f;
	float LastDilation = Fx.WorldDilation();
	bool bLeftStep = false;
	int32 SlowFrames = 0;
	const bool bOver = Fx.RunUntil(&Rider, 3.0f, [&]
	{
		const float D = Fx.WorldDilation();
		MinDilation = FMath::Min(MinDilation, D);
		LargestChange = FMath::Max(LargestChange, FMath::Abs(D - LastDilation));
		LastDilation = D;
		bLeftStep |= Fx.Director->GetPhase() != ELessonPhase::Step;
		RealSlow += RealFrameSeconds;
		++SlowFrames;
		return !Fx.Director->IsSlowMotionActive();
	});
	const float BoardSlow = Fx.Pawn->GetBoardMovement()->GetSimTimeSeconds() - BoardStart;
	AddInfo(FString::Printf(TEXT("Slow motion: %.3f real s over %d frames, %.3f board s, lowest dilation %.3f, largest change a frame %.3f"),
		RealSlow, SlowFrames, BoardSlow, MinDilation, LargestChange));
	if (!TestTrue(TEXT("The slow motion ends by itself"), bOver))
	{
		return false;
	}
	TestFalse(TEXT("The step went on through it (the schedule ran in full)"), bLeftStep);
	TestNearlyEqual(TEXT("It lasts its schedule in real seconds (within a frame)"), RealSlow, LessonTiming::SlowMoTotalSeconds(), RealFrameSeconds + 1e-3f);
	TestNearlyEqual(TEXT("It gets down to 0.6x"), MinDilation, LessonTiming::SlowMoDilation, 1e-3f);
	TestTrue(FString::Printf(TEXT("Eased: the dilation changes by at most 0.12 a frame (%.3f)"), LargestChange), LargestChange <= 0.12f);
	TestTrue(FString::Printf(TEXT("Game time went slower than real time (%.3f board s in %.3f real s)"), BoardSlow, RealSlow), BoardSlow < 0.85f * RealSlow);
	TestEqual(TEXT("The world is back at exactly real time"), Fx.WorldDilation(), 1.0f);
	TestEqual(TEXT("The director asks for real time"), Fx.Director->GetTimeDilation(), 1.0f);
	TestTrue(TEXT("The prompt is gone"), Fx.Director->GetSlowMotionPrompt().IsEmpty());
	Layer.Update(Fx.Director, RealFrameSeconds);
	TestEqual(TEXT("The HUD shows the step's prompt again"), Layer.GetView().Prompt, FString(TEXT("Bar in at 12")));

	// Leaving the lesson in the middle of a slow motion puts real time back at once.
	const bool bNext = Fx.RunUntil(&Rider, 40.0f, [&] { return Fx.Director->IsSlowMotionActive() && Fx.WorldDilation() < 0.9f; });
	if (TestTrue(TEXT("The next send slows time again"), bNext))
	{
		Fx.Director->ExitToFreeRide();
		TestEqual(TEXT("Exit mid slow motion: real time at once"), Fx.WorldDilation(), 1.0f);
	}
	return true;
}

// The slow motion of a step switches off after three Clean (or better) attempts on it, and a Retry
// does not bring it back.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolSlowMoOffAfterThreeClean, "KiteSurf.School.SlowMoOffAfterThreeClean", SchoolSlowMoTest::Flags)

bool FKiteSurfSchoolSlowMoOffAfterThreeClean::RunTest(const FString& Parameters)
{
	FSlowMoFixture Fx;
	const FLessonDef* B2 = LessonCatalog::Find(TEXT("B2"));
	if (!TestTrue(TEXT("Fixture and lesson B2"), Fx.IsValid() && B2))
	{
		return false;
	}
	Fx.Director->BeginLesson(*B2, Fx.Pawn);
	FSendRider Rider;
	Rider.WaitOnFirstStep = Fx.Director;
	int32 SerialAtThree = -1;
	int32 PopsAtThree = -1;
	const bool bThree = Fx.RunUntil(&Rider, 200.0f, [&]
	{
		if (Fx.Director->GetStepIndex() == 1 && Fx.Director->GetSlowMotionCleanAttempts() >= LessonTiming::SlowMoOffAfterCleanAttempts)
		{
			SerialAtThree = Fx.Director->GetSlowMotionSerial();
			PopsAtThree = Rider.Pops;
			return true;
		}
		return Fx.Director->GetOutcome() == ELessonOutcome::Passed;
	});
	if (!TestTrue(TEXT("Three clean attempts on step 2"), bThree && SerialAtThree >= 0))
	{
		AddInfo(FString::Printf(TEXT("Clean attempts %d, step %d, outcome %d"), Fx.Director->GetSlowMotionCleanAttempts(), Fx.Director->GetStepIndex() + 1, int32(Fx.Director->GetOutcome())));
		return false;
	}
	TestEqual(TEXT("Counted exactly three"), Fx.Director->GetSlowMotionCleanAttempts(), 3);
	TestTrue(FString::Printf(TEXT("A slow motion for each send until then (%d for %d pops)"), SerialAtThree, PopsAtThree), SerialAtThree >= 3 && SerialAtThree == PopsAtThree);
	TestFalse(TEXT("Step 2's slow motion is off now"), Fx.Director->IsSlowMotionOnForStep());

	// Two more sends (or the pass): no slow motion, the world at real time throughout once any running one is over.
	Fx.RunUntil(&Rider, 3.0f, [&] { return !Fx.Director->IsSlowMotionActive(); });
	bool bDilated = false;
	const int32 PopsBefore = Rider.Pops;
	Fx.RunUntil(&Rider, 60.0f, [&]
	{
		bDilated |= Fx.WorldDilation() != 1.0f || Fx.Director->IsSlowMotionActive();
		return Rider.Pops >= PopsBefore + 2 || (Fx.Director->GetPhase() == ELessonPhase::Result && Fx.Director->GetOutcome() == ELessonOutcome::Passed);
	});
	TestTrue(TEXT("More sends were ridden"), Rider.Pops > PopsBefore || Fx.Director->GetOutcome() == ELessonOutcome::Passed);
	TestFalse(TEXT("No slow motion after the third clean attempt"), bDilated);
	TestEqual(TEXT("No new slow motion started"), Fx.Director->GetSlowMotionSerial(), SerialAtThree);

	// A Retry keeps the count: back on step 2 the slow motion stays off.
	TestTrue(TEXT("Retry"), Fx.Director->Retry());
	FSendRider Again;
	Again.WaitOnFirstStep = Fx.Director;
	const bool bStep2 = Fx.RunUntil(&Again, 40.0f, [&] { return Fx.Director->GetStepIndex() == 1; });
	TestTrue(TEXT("Step 2 again after the Retry"), bStep2);
	TestFalse(TEXT("Its slow motion stays off"), Fx.Director->IsSlowMotionOnForStep() || Fx.Director->IsSlowMotionActive());
	TestEqual(TEXT("The world at real time"), Fx.WorldDilation(), 1.0f);
	return true;
}

// Slow motion is never on outside a lesson: not on a free ride with an idle director, not on a lesson
// ridden with the slow-motion assist off, not on a lesson step without a decision point; and the
// world is back at real time when the director goes away in the middle of one.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolSlowMoNeverOutsideLesson, "KiteSurf.School.SlowMoNeverOutsideLesson", SchoolSlowMoTest::Flags)

bool FKiteSurfSchoolSlowMoNeverOutsideLesson::RunTest(const FString& Parameters)
{
	const FLessonDef* B2 = LessonCatalog::Find(TEXT("B2"));
	const FLessonDef* B1 = LessonCatalog::Find(TEXT("B1"));
	if (!TestTrue(TEXT("Lessons B1 and B2"), B1 && B2))
	{
		return false;
	}

	// A free ride: the same sends and pops, a director in the world that runs no lesson.
	{
		FSlowMoFixture Fx;
		if (!TestTrue(TEXT("Fixture"), Fx.IsValid()))
		{
			return false;
		}
		FSendRider Rider;
		bool bDilated = false;
		Fx.RunUntil(&Rider, 30.0f, [&]
		{
			bDilated |= Fx.WorldDilation() != 1.0f || Fx.Director->IsSlowMotionActive() || Fx.Director->GetTimeDilation() != 1.0f;
			return false;
		});
		TestTrue(TEXT("Free ride: the rider jumped"), Rider.Pops >= 2);
		TestFalse(TEXT("Free ride: never slowed"), bDilated);
		TestFalse(TEXT("Free ride: no step to slow"), Fx.Director->IsSlowMotionOnForStep());
		TestEqual(TEXT("Free ride: no slow motion started"), Fx.Director->GetSlowMotionSerial(), 0);
	}

	// B2 with the slow-motion assist turned off for the run (S5's "wind and assists").
	{
		FSlowMoFixture Fx;
		FLessonRunOptions Options;
		Options.bOverrideAssists = true;
		Options.Assists = B2->Setup.Assists;
		Options.Assists.bSlowMotion = false;
		TestTrue(TEXT("B2 without slow motion begins"), Fx.Director->BeginLesson(*B2, Fx.Pawn, Options));
		FSendRider Rider;
		Rider.WaitOnFirstStep = Fx.Director;
		bool bDilated = false;
		const bool bStep2 = Fx.RunUntil(&Rider, 70.0f, [&]
		{
			bDilated |= Fx.WorldDilation() != 1.0f || Fx.Director->IsSlowMotionActive();
			return Fx.Director->GetStepIndex() == 1 && Rider.Pops >= 3;
		});
		TestTrue(TEXT("Assist off: step 2 ridden with sends"), bStep2);
		TestFalse(TEXT("Assist off: step 2's slow motion is off"), Fx.Director->IsSlowMotionOnForStep());
		TestFalse(TEXT("Assist off: never slowed"), bDilated);
	}

	// B1: jumps, but no decision point on any step.
	{
		FSlowMoFixture Fx;
		Fx.Director->BeginLesson(*B1, Fx.Pawn);
		FSendRider Rider;
		bool bDilated = false;
		Fx.RunUntil(&Rider, 25.0f, [&]
		{
			bDilated |= Fx.WorldDilation() != 1.0f || Fx.Director->IsSlowMotionActive() || Fx.Director->IsSlowMotionOnForStep();
			return false;
		});
		TestTrue(TEXT("B1: the rider jumped"), Rider.Pops >= 1);
		TestFalse(TEXT("B1: never slowed"), bDilated);
	}

	// The director destroyed (a new lesson from the console, the level going) in the middle of a slow motion.
	{
		FSlowMoFixture Fx;
		Fx.Director->BeginLesson(*B2, Fx.Pawn);
		FSendRider Rider;
		Rider.WaitOnFirstStep = Fx.Director;
		const bool bSlow = Fx.RunUntil(&Rider, 40.0f, [&] { return Fx.Director->IsSlowMotionActive() && Fx.WorldDilation() < 0.9f; });
		if (TestTrue(TEXT("B2's slow motion runs"), bSlow))
		{
			// A new run of the lesson (Retry) first: real time at once.
			Fx.Director->Retry();
			TestEqual(TEXT("Retry mid slow motion: real time at once"), Fx.WorldDilation(), 1.0f);
			Fx.RunUntil(&Rider, 40.0f, [&] { return Fx.Director->IsSlowMotionActive() && Fx.WorldDilation() < 0.9f; });
			TestTrue(TEXT("Slowed again"), Fx.WorldDilation() < 0.9f);
			Fx.Director->Destroy();
			TestEqual(TEXT("Director destroyed mid slow motion: real time at once"), Fx.WorldDilation(), 1.0f);
		}
	}
	return true;
}

namespace SchoolSlowMoTest
{
	/** The first jump of a B3 run (its first step, "dive the kite now", slows time on the way down when bSlowMotion). */
	struct FFirstJump
	{
		bool bValid = false;
		FJumpRecord Record;
		/** Display frames from the take-off to the touchdown, and the lowest world dilation in them. */
		int32 AirFrames = 0;
		float MinDilation = 1.0f;
		int32 SlowMotions = 0;
	};

	FFirstJump RideFirstB3Jump(const FLessonDef& B3, bool bSlowMotion)
	{
		FFirstJump Out;
		FSlowMoFixture Fx;
		if (!Fx.IsValid())
		{
			return Out;
		}
		FLessonRunOptions Options;
		Options.bOverrideAssists = true;
		Options.Assists = B3.Setup.Assists;
		Options.Assists.bSlowMotion = bSlowMotion;
		if (!Fx.Director->BeginLesson(B3, Fx.Pawn, Options))
		{
			return Out;
		}
		FSendRider Rider;
		Rider.bHandsStillInAir = true;
		UTrickTrackerComponent* Tracker = Fx.Pawn->GetTrickTracker();
		const int32 RecordsBefore = Tracker ? Tracker->GetJumpRecordCount() : 0;
		Fx.RunUntil(&Rider, 40.0f, [&]
		{
			if (Fx.Pawn->GetBoardMovement()->GetBoardState() == EBoardState::Airborne && Rider.Pops == 1)
			{
				++Out.AirFrames;
				Out.MinDilation = FMath::Min(Out.MinDilation, Fx.WorldDilation());
			}
			return Tracker && Tracker->GetJumpRecordCount() > RecordsBefore;
		});
		Out.SlowMotions = Fx.Director->GetSlowMotionSerial();
		Out.bValid = Tracker && Tracker->GetLastJumpRecord(Out.Record) && Rider.Pops == 1;
		return Out;
	}
}

// Determinism: the simulation runs in fixed 240 Hz steps from the scaled frame time, so slow motion
// spreads the same ride over more frames without changing it. The same scripted B3 ride, once with
// the slow-motion assist (time slowed to 0.6x on the way down) and once without, takes off, peaks
// and lands at the same sim times and the same height.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolSlowMoKeepsSimDeterministic, "KiteSurf.School.SlowMoKeepsSimDeterministic", SchoolSlowMoTest::Flags)

bool FKiteSurfSchoolSlowMoKeepsSimDeterministic::RunTest(const FString& Parameters)
{
	const FLessonDef* B3 = LessonCatalog::Find(TEXT("B3"));
	if (!TestNotNull(TEXT("B3"), B3))
	{
		return false;
	}
	const FFirstJump Real = RideFirstB3Jump(*B3, false);
	const FFirstJump Slow = RideFirstB3Jump(*B3, true);
	if (!TestTrue(TEXT("Both rides land a first jump"), Real.bValid && Slow.bValid))
	{
		return false;
	}
	const FJumpRecord& A = Real.Record;
	const FJumpRecord& B = Slow.Record;
	AddInfo(FString::Printf(TEXT("Without slow motion: take-off %.4f s, apex %.4f s at %.2f m, touchdown %.4f s, %d air frames, grade %s"),
		A.TakeoffTimeSeconds, A.ApexTimeSeconds, KiteUnits::CmToM(A.ApexHeightCm), A.LandingTimeSeconds, Real.AirFrames, *UEnum::GetValueAsString(A.Grade)));
	AddInfo(FString::Printf(TEXT("With slow motion:    take-off %.4f s, apex %.4f s at %.2f m, touchdown %.4f s, %d air frames, lowest dilation %.3f, grade %s"),
		B.TakeoffTimeSeconds, B.ApexTimeSeconds, KiteUnits::CmToM(B.ApexHeightCm), B.LandingTimeSeconds, Slow.AirFrames, Slow.MinDilation, *UEnum::GetValueAsString(B.Grade)));

	// The slow motion really ran in the second ride, inside the jump.
	TestEqual(TEXT("No slow motion without the assist"), Real.SlowMotions, 0);
	TestEqual(TEXT("No dilation without the assist"), Real.MinDilation, 1.0f);
	TestEqual(TEXT("One slow motion with it, on the way down"), Slow.SlowMotions, 1);
	TestTrue(FString::Printf(TEXT("It slowed the jump to 0.6x (lowest %.3f)"), Slow.MinDilation), Slow.MinDilation < 0.61f);
	TestTrue(FString::Printf(TEXT("The jump took more frames slowed (%d against %d)"), Slow.AirFrames, Real.AirFrames), Slow.AirFrames >= Real.AirFrames + 10);

	// The same ride in sim time: a step is 1/240 s, so 1e-4 s is far below one step.
	TestTrue(TEXT("A real jump (over 1 m)"), A.ApexHeightCm > 100.0f);
	TestNearlyEqual(TEXT("Same take-off time (sim s)"), B.TakeoffTimeSeconds, A.TakeoffTimeSeconds, 1e-4f);
	TestNearlyEqual(TEXT("Same apex time (sim s)"), B.ApexTimeSeconds, A.ApexTimeSeconds, 1e-4f);
	TestNearlyEqual(TEXT("Same apex height (cm)"), B.ApexHeightCm, A.ApexHeightCm, 0.01f);
	TestNearlyEqual(TEXT("Same touchdown time (sim s)"), B.LandingTimeSeconds, A.LandingTimeSeconds, 1e-4f);
	TestNearlyEqual(TEXT("Same airtime (s)"), B.AirtimeSeconds, A.AirtimeSeconds, 1e-4f);
	TestNearlyEqual(TEXT("Same distance (cm)"), B.DistanceCm, A.DistanceCm, 0.01f);
	TestNearlyEqual(TEXT("Same sink rate at touchdown (cm/s)"), B.SinkRateCmS, A.SinkRateCmS, 0.01f);
	TestEqual(TEXT("Same landing grade"), B.Grade, A.Grade);
	return true;
}

#endif
