#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "InputActionValue.h"
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

// The kite school's lessons ridden (docs/tutorials.md S6, section 2.1): for each available lesson a
// competent scripted ride that passes through ALessonDirector and a ride with the lesson's typical
// mistake that fails with its feedback line, both from the player's default start (the bar in the
// middle with BAR TO MIDDLE). The hands work like a player's: the bar through the player's
// spring-loaded sheet input, the steering, the edge and the jump button, stepped at 60 fps with the
// pawn's 240 Hz physics. The lessons' thresholds and set-ups were tuned on these rides; the numbers
// they measure are in docs/tutorials.md 2.1. Then the evaluator rules the rides needed, on synthetic
// telemetry.

namespace SchoolRideTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	const float RideFrameSeconds = 1.0f / 60.0f;

	/** A rider and a lesson director in a throwaway world; results go to a subsystem that never writes to disk. */
	struct FLessonRideFixture
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		ALessonDirector* Director = nullptr;
		ULessonSubsystem* Lessons = nullptr;
		float Time = 0.0f;

		FLessonRideFixture()
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
					Wind->BaseWind = FVector(KiteUnits::KnotsToCmS(15.0f), 0.0f, 0.0f);
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
				Director->bApplyTimeDilation = false;
			}
		}

		~FLessonRideFixture()
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
			Pawn->Tick(RideFrameSeconds);
			Director->UpdateLesson(RideFrameSeconds);
			Time += RideFrameSeconds;
		}

		void Log(const TCHAR* Tag) const
		{
			const UKiteComponent* Kite = Pawn->GetKite();
			const UBoardMovementComponent* Board = Pawn->GetBoardMovement();
			UE_LOG(LogTemp, Display, TEXT("RIDE %s t=%5.2f state=%d spd=%5.2f vy=%6.2f yaw=%4.0f h=%4.2f clk=%5.1f el=%5.1f T=%5.0f bar=%.2f steer=%+.2f edge=%+.2f | step=%d phase=%d out=%d val=%.2f prog=%.2f fail=%d"),
				Tag, Time, int32(Board->GetBoardState()), KiteUnits::CmToM(Board->Velocity.Size2D()), KiteUnits::CmToM(Board->Velocity.Y),
				Pawn->GetActorRotation().Yaw, KiteUnits::CmToM(Pawn->GetActorLocation().Z - Board->GetWaterSurfaceHeightCm()),
				Kite->GetClockDeg(), Kite->GetElevationDeg(), Kite->GetLineTensionN(), Pawn->GetCurrentSheetInput(), Pawn->GetCurrentSteerInput(),
				Board->GetEdgeInput(), Director->GetStepIndex(), int32(Director->GetPhase()), int32(Director->GetOutcome()),
				Director->GetObjectiveValue(), Director->GetObjectiveProgress(), Director->GetStepFailures());
		}
	};

	/** The player's hands: the bar through the player's spring-loaded sheet input (BAR TO MIDDLE), the steering, the edge and the jump button. */
	struct FHands
	{
		AKiteRiderPawn* Pawn = nullptr;
		float LastClock = 0.0f;
		float LastElevation = 0.0f;
		float ClockRate = 0.0f;
		float ElevationRate = 0.0f;
		bool bHasLast = false;

		explicit FHands(AKiteRiderPawn* InPawn) : Pawn(InPawn) {}

		UKiteComponent* Kite() const { return Pawn->GetKite(); }
		UBoardMovementComponent* Board() const { return Pawn->GetBoardMovement(); }
		float Clock() const { return Kite()->GetClockDeg(); }
		float Elevation() const { return Kite()->GetElevationDeg(); }
		bool InAir() const { return Board()->GetBoardState() == EBoardState::Airborne; }

		void Observe(float Dt)
		{
			const float C = Clock();
			const float E = Elevation();
			ClockRate = bHasLast && Dt > 0.0f ? (C - LastClock) / Dt : 0.0f;
			ElevationRate = bHasLast && Dt > 0.0f ? (E - LastElevation) / Dt : 0.0f;
			LastClock = C;
			LastElevation = E;
			bHasLast = true;
		}

		/** The bar towards Target (0 out, 1 in) through the player's sheet input: full input is fully in or out from the middle. */
		void Bar(float Target)
		{
			const float Neutral = Pawn->BarNeutralSheet;
			const float Input = Target >= Neutral ? (Target - Neutral) / FMath::Max(1.0f - Neutral, 0.01f) : (Target - Neutral) / FMath::Max(Neutral, 0.01f);
			Pawn->OnSheetTriggered(FInputActionValue(FMath::Clamp(Input, -1.0f, 1.0f)));
		}

		void Steer(float Axis) { Pawn->SteerKite(FMath::Clamp(Axis, -1.0f, 1.0f)); }

		/** A damped hand towards a clock position. */
		void SteerToClock(float Target, float GainDeg = 30.0f, float MaxSteer = 0.6f, float DampingSeconds = 0.6f)
		{
			Steer(FMath::Clamp((Target - Clock() - DampingSeconds * ClockRate) / GainDeg, -MaxSteer, MaxSteer));
		}

		void Edge(float Value) { Pawn->EdgeBoard(Value); }
		void Load(bool bHeld) { Pawn->SetLoadHeld(bHeld); }
		bool Pop() { return Pawn->ReleaseLoadAndPop(); }

		/** +1 riding to the right looking downwind (the wind blows along +X), -1 to the left, 0 when stopped. */
		int32 Tack() const
		{
			const float Vy = Board()->Velocity.Y;
			return FMath::Abs(Vy) < 50.0f ? 0 : (Vy > 0.0f ? 1 : -1);
		}

		/** Degrees the travel points upwind of the beam reach on its tack (negative: downwind of it). */
		float CourseDeg() const
		{
			const FVector V = Board()->Velocity;
			const float YawDeg = FMath::RadiansToDegrees(FMath::Atan2(V.Y, V.X));
			return V.Y >= 0.0f ? YawDeg - 90.0f : -90.0f - YawDeg;
		}

		float SpeedMS() const { return KiteUnits::CmToM(Board()->Velocity.Size2D()); }

		/** Edges to hold a course this far upwind of the beam on the tack it is on (a proportional hand on the edge). */
		void HoldCourse(float TargetDeg, float Gain = 0.04f, float MaxEdge = 0.6f)
		{
			const int32 T = Tack();
			if (T == 0)
			{
				Edge(0.0f);
				return;
			}
			Edge(static_cast<float>(T) * FMath::Clamp((TargetDeg - CourseDeg()) * Gain, -MaxEdge, MaxEdge));
		}
	};

	/** One lesson run on a fixture: begin it, then ride a policy frame by frame until it passes, an attempt fails or time runs out. */
	struct FLessonRun
	{
		FLessonRideFixture Fx;
		FHands H;
		const FLessonDef* Lesson = nullptr;
		FString Tag;
		float StartBar = -1.0f;

		explicit FLessonRun(FName Id, const TCHAR* InTag) : H(nullptr), Tag(InTag)
		{
			H.Pawn = Fx.Pawn;
			Lesson = LessonCatalog::Find(Id);
			if (Fx.IsValid() && Lesson && Fx.Director->BeginLesson(*Lesson, Fx.Pawn))
			{
				StartBar = Fx.Pawn->GetCurrentSheetInput();
			}
		}

		bool IsValid() const { return Fx.IsValid() && Lesson && StartBar >= 0.0f; }

		/**
		 * Rides Policy(Run) every frame for at most Seconds; stops at a pass, at the first failed attempt
		 * when bStopOnFailure, or at a lesson Failed. Returns the outcome it stopped on (None on timeout).
		 */
		template <typename FPolicy>
		ELessonOutcome Ride(float Seconds, bool bStopOnFailure, FPolicy&& Policy)
		{
			for (float T = 0.0f; T < Seconds; T += RideFrameSeconds)
			{
				H.Observe(RideFrameSeconds);
				Policy(*this);
				Fx.Frame();
				if (FMath::Fmod(Fx.Time, 0.5f) < RideFrameSeconds)
				{
					Fx.Log(*Tag);
				}
				const ELessonOutcome Outcome = Fx.Director->GetOutcome();
				if (Outcome == ELessonOutcome::Passed || Outcome == ELessonOutcome::Failed || (bStopOnFailure && Outcome == ELessonOutcome::AttemptFailed))
				{
					Fx.Log(*Tag);
					UE_LOG(LogTemp, Display, TEXT("RIDE %s stopped at %.2f s: outcome %d, step %d, fault %s \"%s\", value %.2f, stars %d"), *Tag, Fx.Time, int32(Outcome),
						Fx.Director->GetStepIndex() + 1, *Fx.Director->GetLastFaultId().ToString(), *Fx.Director->GetLastFaultLine().ToString(), Fx.Director->GetObjectiveValue(), Fx.Director->GetStars());
					return Outcome;
				}
			}
			UE_LOG(LogTemp, Display, TEXT("RIDE %s timed out at %.2f s on step %d (%d failures, last fault %s)"), *Tag, Fx.Time, Fx.Director->GetStepIndex() + 1,
				Fx.Director->GetStepFailures(), *Fx.Director->GetLastFaultId().ToString());
			return ELessonOutcome::None;
		}

		float Step() const { return Fx.Director->GetStepIndex(); }
		ALessonDirector* Director() const { return Fx.Director; }
	};

}

using namespace SchoolRideTest;

namespace SchoolRideTest
{
	/**
	 * A1's dives: from the kite up at 12, steer it down one side until it is TurnBackDeg high, then
	 * back up past UpDeg; a moment there, then the other side.
	 */
	struct FDiver
	{
		enum class EPhase { Up, Dive, Return };
		EPhase Phase = EPhase::Up;
		float PhaseSeconds = 0.0f;
		float Side = 1.0f;
		/** Turn back once the kite is this low (deg). */
		float TurnBackDeg = 62.0f;
		/** Up again: over this (deg). */
		float UpDeg = 72.0f;
		float RestSeconds = 0.3f;
		/**
		 * Part of the bar: the kite travels round the window that way. A full bar reversed asks the assist for a heading
		 * more than 180 deg round, which it reaches through the bottom of the window: the kite goes into the water.
		 */
		float Steer = 0.35f;
		float BarPos = 0.6f;

		void Step(FLessonRun& Run)
		{
			FHands& H = Run.H;
			PhaseSeconds += RideFrameSeconds;
			H.Bar(BarPos);
			H.Edge(0.0f);
			switch (Phase)
			{
			case EPhase::Up:
				H.SteerToClock(0.0f, 30.0f, 0.5f, 0.3f);
				if (PhaseSeconds >= RestSeconds && H.Elevation() >= UpDeg)
				{
					Phase = EPhase::Dive;
					PhaseSeconds = 0.0f;
				}
				break;
			case EPhase::Dive:
				H.Steer(Side * Steer);
				if (H.Elevation() <= TurnBackDeg)
				{
					Phase = EPhase::Return;
					PhaseSeconds = 0.0f;
				}
				break;
			case EPhase::Return:
				H.Steer(-Side * Steer);
				if (H.Elevation() >= UpDeg)
				{
					Phase = EPhase::Up;
					PhaseSeconds = 0.0f;
					Side = -Side;
				}
				break;
			}
		}
	};
}

// Lesson A1, kite power dive: six dives to under 55 deg and back over 70 deg within the window pass
// (tuned: 14 kn, 10 s; a dive over a floating rider takes about 8 s); a dive taken too deep fails with
// "Turn it back up before 45°".
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolRideA1, "KiteSurf.School.RideA1", SchoolRideTest::Flags)

bool FKiteSurfSchoolRideA1::RunTest(const FString& Parameters)
{
	{
		FLessonRun Run(TEXT("A1"), TEXT("A1good"));
		if (!TestTrue(TEXT("A1 begins"), Run.IsValid()))
		{
			return false;
		}
		TestNearlyEqual(TEXT("Player default: the bar starts at the middle"), Run.StartBar, 0.5f, 1e-3f);
		FDiver Diver;
		const ELessonOutcome Outcome = Run.Ride(120.0f, false, [&](FLessonRun& R) { Diver.Step(R); });
		TestEqual(TEXT("Competent dives pass A1"), Outcome, ELessonOutcome::Passed);
		AddInfo(FString::Printf(TEXT("A1: six dives in %.0f s"), Run.Fx.Time));
	}
	{
		FLessonRun Run(TEXT("A1"), TEXT("A1bad"));
		FDiver Diver;
		Diver.TurnBackDeg = 40.0f;
		const ELessonOutcome Outcome = Run.Ride(60.0f, true, [&](FLessonRun& R) { Diver.Step(R); });
		TestEqual(TEXT("A dive too deep fails an attempt"), Outcome, ELessonOutcome::AttemptFailed);
		TestEqual(TEXT("with the line"), Run.Director()->GetLastFaultLine().ToString(), FString(TEXT("Turn it back up before 45°")));
	}
	return true;
}

namespace SchoolRideTest
{
	/** A2: dive the kite to get going, ride a leg on the tack, steer the kite across to turn, ride the other way. */
	struct FWaterStarter
	{
		enum class EPhase { Dive, Ride, Turn };
		EPhase Phase = EPhase::Dive;
		float PhaseSeconds = 0.0f;
		/** +1 dives towards where the board points (right, the tack's side). */
		float DiveSide = 1.0f;
		float DiveClockDeg = 60.0f;
		/** The A2 mistake: the weight on the front foot once up (-1 tail .. +1 nose). */
		float RideWeight = 0.0f;
		bool bDived = false;
		float RideClockDeg = 60.0f;
		float RideBar = 1.0f;
		float LegSeconds = 12.0f;
		float Side = 1.0f;

		void Step(FLessonRun& Run)
		{
			FHands& H = Run.H;
			PhaseSeconds += RideFrameSeconds;
			switch (Phase)
			{
			case EPhase::Dive:
				H.Bar(0.8f);
				if (!bDived && DiveSide * H.Clock() < DiveClockDeg)
				{
					H.Steer(DiveSide);
				}
				else
				{
					bDived = true;
					H.SteerToClock(DiveSide * DiveClockDeg, 40.0f, 0.5f, 0.3f);
				}
				H.HoldCourse(0.0f);
				if (H.Board()->GetBoardState() == EBoardState::Planing && PhaseSeconds > 1.0f)
				{
					Phase = EPhase::Ride;
					PhaseSeconds = 0.0f;
				}
				break;
			case EPhase::Ride:
				H.Bar(RideBar);
				H.SteerToClock(Side * RideClockDeg, 40.0f, 0.5f, 0.3f);
				H.HoldCourse(0.0f);
				H.Board()->SetWeightShift(RideWeight);
				if (PhaseSeconds > LegSeconds)
				{
					Phase = EPhase::Turn;
					PhaseSeconds = 0.0f;
					Side = -Side;
				}
				break;
			case EPhase::Turn:
				H.Bar(RideBar);
				H.SteerToClock(Side * RideClockDeg, 20.0f, 1.0f, 0.3f);
				H.Edge(0.0f);
				if (H.Tack() == static_cast<int32>(Side) && PhaseSeconds > 1.0f)
				{
					Phase = EPhase::Ride;
					PhaseSeconds = 0.0f;
				}
				break;
			}
		}
	};
}

// Lesson A2, water start: a dive towards where the board points gets the rider planing within 6 s
// (3.8 s), then 50 m on each tack pass; the weight on the nose once up carves the board off the kite,
// the ride is lost, and the line is "Point the nose at the kite".
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolRideA2, "KiteSurf.School.RideA2", SchoolRideTest::Flags)

bool FKiteSurfSchoolRideA2::RunTest(const FString& Parameters)
{
	{
		FLessonRun Run(TEXT("A2"), TEXT("A2good"));
		if (!TestTrue(TEXT("A2 begins"), Run.IsValid()))
		{
			return false;
		}
		TestNearlyEqual(TEXT("Player default: the bar starts at the middle"), Run.StartBar, 0.5f, 1e-3f);
		FWaterStarter Starter;
		const ELessonOutcome Outcome = Run.Ride(120.0f, false, [&](FLessonRun& R) { Starter.Step(R); });
		TestEqual(TEXT("A competent water start passes A2"), Outcome, ELessonOutcome::Passed);
		AddInfo(FString::Printf(TEXT("A2: passed after %.0f s"), Run.Fx.Time));
	}
	{
		FLessonRun Run(TEXT("A2"), TEXT("A2bad"));
		FWaterStarter Starter;
		Starter.RideWeight = 1.0f;
		const ELessonOutcome Outcome = Run.Ride(60.0f, true, [&](FLessonRun& R) { Starter.Step(R); });
		TestEqual(TEXT("Weight on the nose once up turns the board off the kite and loses the ride"), Outcome, ELessonOutcome::AttemptFailed);
		TestEqual(TEXT("with the line"), Run.Director()->GetLastFaultLine().ToString(), FString(TEXT("Point the nose at the kite")));
	}
	return true;
}

namespace SchoolRideTest
{
	/** A3: the kite flown at about 45 deg on the tack's side, the bar worked to hold a speed. */
	struct FSpeedHolder
	{
		float ClockDeg = 47.0f;
		float TargetMS = 6.17f;
		float BarBase = 0.78f;
		float BarGain = 0.12f;
		float BarI = 0.0f;

		void Step(FLessonRun& Run)
		{
			FHands& H = Run.H;
			const float Side = H.Tack() < 0 ? -1.0f : 1.0f;
			H.SteerToClock(Side * ClockDeg, 40.0f, 0.5f, 0.3f);
			H.HoldCourse(0.0f);
			const float Error = TargetMS - H.SpeedMS();
			BarI = FMath::Clamp(BarI + Error * RideFrameSeconds * 0.05f, -0.15f, 0.15f);
			H.Bar(FMath::Clamp(BarBase + BarGain * Error + BarI, 0.55f, 0.9f));
		}
	};
}

// Lesson A3, speed control: the kite at 45 deg and the bar worked to hold the target speed (tuned: 12 kn)
// for 20 s pass; the kite parked high never gets past step 1. A held objective has no failed attempt:
// its feedback is the HUD's hint line, which shows "Kite too high: fly it at 45°".
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolRideA3, "KiteSurf.School.RideA3", SchoolRideTest::Flags)

bool FKiteSurfSchoolRideA3::RunTest(const FString& Parameters)
{
	{
		FLessonRun Run(TEXT("A3"), TEXT("A3good"));
		if (!TestTrue(TEXT("A3 begins"), Run.IsValid()))
		{
			return false;
		}
		TestNearlyEqual(TEXT("Player default: the bar starts at the middle"), Run.StartBar, 0.5f, 1e-3f);
		FSpeedHolder Holder;
		const ELessonOutcome Outcome = Run.Ride(120.0f, false, [&](FLessonRun& R) { Holder.Step(R); });
		TestEqual(TEXT("Holding the speed with the bar passes A3"), Outcome, ELessonOutcome::Passed);
		AddInfo(FString::Printf(TEXT("A3: held %.2f m/s at the pass, %d star(s)"), Run.Director()->GetObjectiveValue(), Run.Director()->GetStars()));
	}
	{
		FLessonRun Run(TEXT("A3"), TEXT("A3bad"));
		FSpeedHolder Holder;
		Holder.ClockDeg = 12.0f;
		FLessonHUDLayer Layer;
		TSet<FString> Hints;
		const ELessonOutcome Outcome = Run.Ride(30.0f, true, [&](FLessonRun& R)
		{
			Holder.Step(R);
			Layer.Update(R.Director(), RideFrameSeconds);
			if (!Layer.GetView().Hint.IsEmpty())
			{
				Hints.Add(Layer.GetView().Hint);
			}
		});
		TestEqual(TEXT("The kite parked high does not pass"), Outcome, ELessonOutcome::None);
		TestEqual(TEXT("still on step 1"), Run.Director()->GetStepIndex(), 0);
		TestTrue(FString::Printf(TEXT("and the HUD's hint shows the lesson's line (hints shown: %s)"), *FString::Join(Hints.Array(), TEXT(" | "))),
			Hints.Contains(TEXT("Kite too high: fly it at 45°")));
	}
	return true;
}

namespace SchoolRideTest
{
	/**
	 * Riding on a tack and turning (A4, A5, A7): the kite held on the tack's side and the board on a
	 * course; a turn slows down first (bar out, heading up), flies the kite over the top with part of
	 * the bar, then dives it down the new side with the bar in for the pull to get the rider going, the
	 * board following the kite (CarveEdge carves it round downwind once the kite has crossed).
	 */
	struct FTacker
	{
		enum class EPhase { Ride, Slow, Cross, Dive };
		EPhase Phase = EPhase::Ride;
		float PhaseSeconds = 0.0f;
		/** The tack being ridden: +1 the kite on the right. Set at the start from the kite's side. */
		float Side = 0.0f;
		float ClockDeg = 55.0f;
		/**
		 * Most bar while holding the kite. More than about half a bar reversed against a kite flying fast along
		 * the window edge asks the assist for a heading it reaches through the bottom of the window.
		 */
		float HoldSteer = 0.4f;
		float RideBar = 0.85f;
		float CourseDeg = 0.0f;
		/** Seconds on each tack before turning; 0 never turns. */
		float LegSeconds = 0.0f;
		/** Turn when this returns true (overrides LegSeconds when set). */
		TFunction<bool(FLessonRun&)> TurnWhen;
		/** Before the turn: bar out and head up until slower than this (m/s); 0 turns at speed. */
		float SlowToMS = 5.0f;
		/** Bar over to fly the kite across 12 (part of the bar: it goes over the top). */
		float CrossSteer = 0.6f;
		float CrossBar = 0.6f;
		/** Then full bar down the new side to this clock with the bar in, for the pull to get going (a water start's dive). */
		float DiveToClockDeg = 45.0f;
		float DiveBar = 0.85f;
		/** Edge towards downwind once the kite has crossed 12 (0..1). */
		float CarveEdge = 0.0f;
		int32 Turns = 0;

		void StartTurn(float FromSide)
		{
			Side = FromSide;
			Phase = SlowToMS > 0.0f ? EPhase::Slow : EPhase::Cross;
			PhaseSeconds = 0.0f;
		}

		void Step(FLessonRun& Run)
		{
			FHands& H = Run.H;
			PhaseSeconds += RideFrameSeconds;
			if (Side == 0.0f)
			{
				Side = H.Clock() >= 0.0f ? 1.0f : -1.0f;
			}
			const bool bCrossed = -Side * H.Clock() > 0.0f;
			switch (Phase)
			{
			case EPhase::Ride:
			{
				H.Bar(RideBar);
				H.SteerToClock(Side * ClockDeg, 40.0f, HoldSteer, 0.3f);
				H.HoldCourse(CourseDeg);
				const bool bTurn = TurnWhen ? TurnWhen(Run) : (LegSeconds > 0.0f && PhaseSeconds > LegSeconds);
				if (bTurn)
				{
					StartTurn(Side);
				}
				break;
			}
			case EPhase::Slow:
				H.Bar(0.2f);
				H.SteerToClock(Side * 40.0f, 40.0f, HoldSteer, 0.3f);
				H.HoldCourse(35.0f);
				if (H.SpeedMS() < SlowToMS || PhaseSeconds > 5.0f)
				{
					Phase = EPhase::Cross;
					PhaseSeconds = 0.0f;
				}
				break;
			case EPhase::Cross:
				H.Bar(CrossBar);
				H.Steer(-Side * CrossSteer);
				H.Edge(bCrossed ? -Side * CarveEdge : 0.0f);
				if (-Side * H.Clock() > 15.0f)
				{
					Phase = EPhase::Dive;
					PhaseSeconds = 0.0f;
				}
				break;
			case EPhase::Dive:
				H.Bar(DiveBar);
				H.Steer(-Side);
				H.Edge(-Side * CarveEdge);
				if (-Side * H.Clock() > DiveToClockDeg || PhaseSeconds > 4.0f)
				{
					Side = -Side;
					Phase = EPhase::Ride;
					PhaseSeconds = 0.0f;
					++Turns;
				}
				break;
			}
		}
	};
}

// Lesson A4, upwind: planing first, then a course 25 deg upwind of the beam on each tack gains 50 m
// on each; carving up hard at once collapses the speed and fails with "Ease the edge".
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolRideA4, "KiteSurf.School.RideA4", SchoolRideTest::Flags)

bool FKiteSurfSchoolRideA4::RunTest(const FString& Parameters)
{
	{
		FLessonRun Run(TEXT("A4"), TEXT("A4good"));
		if (!TestTrue(TEXT("A4 begins"), Run.IsValid()))
		{
			return false;
		}
		TestNearlyEqual(TEXT("Player default: the bar starts at the middle"), Run.StartBar, 0.5f, 1e-3f);
		FTacker Tacker;
		Tacker.CourseDeg = 25.0f;
		// Turn once this tack's leg has counted (the objective's reference moved).
		int32 CountSeen = 0;
		Tacker.TurnWhen = [&CountSeen](FLessonRun& R)
		{
			const int32 Count = R.Director()->GetStepIndex() == 1 ? FMath::RoundToInt(R.Director()->GetObjectiveProgress() * 2.0f) : 0;
			const bool bTurn = Count > CountSeen;
			CountSeen = FMath::Max(CountSeen, Count);
			return bTurn;
		};
		const ELessonOutcome Outcome = Run.Ride(150.0f, false, [&](FLessonRun& R) { Tacker.Step(R); });
		TestEqual(TEXT("Planing upwind on both tacks passes A4"), Outcome, ELessonOutcome::Passed);
		AddInfo(FString::Printf(TEXT("A4: passed after %.0f s, %d failed legs"), Run.Fx.Time, Run.Director()->GetStepFailures()));
	}
	{
		FLessonRun Run(TEXT("A4"), TEXT("A4bad"));
		FTacker Tacker;
		Tacker.CourseDeg = 55.0f;
		const ELessonOutcome Outcome = Run.Ride(60.0f, true, [&](FLessonRun& R) { Tacker.Step(R); });
		TestEqual(TEXT("Carving straight up loses the leg"), Outcome, ELessonOutcome::AttemptFailed);
		TestEqual(TEXT("with the line"), Run.Director()->GetLastFaultLine().ToString(), FString(TEXT("Ease the edge")));
	}
	return true;
}

namespace SchoolRideTest
{
	/** A5: slow down with the bar out, fly the kite to 12, then turn three times riding away each time. */
	struct FTransitioner
	{
		FTacker Tacker;
		float SlowCourseDeg = 35.0f;

		void Step(FLessonRun& Run)
		{
			FHands& H = Run.H;
			const int32 StepIndex = Run.Director()->GetStepIndex();
			if (StepIndex == 0)
			{
				H.Bar(0.2f);
				H.SteerToClock(H.Tack() < 0 ? -40.0f : 40.0f, 40.0f, 0.5f, 0.3f);
				H.HoldCourse(SlowCourseDeg);
				return;
			}
			if (StepIndex == 1)
			{
				H.Bar(0.6f);
				H.SteerToClock(0.0f, 30.0f, 0.8f, 0.3f);
				H.Edge(0.0f);
				return;
			}
			if (!bStarted)
			{
				// Step 3 begins with the kite at 12: carry it on over to the other side and carve round.
				bStarted = true;
				Tacker.Side = 1.0f;
				Tacker.Phase = FTacker::EPhase::Cross;
			}
			Tacker.Step(Run);
		}

		bool bStarted = false;
	};
}

// Lesson A5, transition (tuned: 14 kn): slowing down, the kite to 12, then three turns with the kite
// flown over and dived down the new side keep 70% of the speed and pass; steering the kite slowly
// through the turn sinks the rider and fails with "Steer the kite faster through the turn".
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolRideA5, "KiteSurf.School.RideA5", SchoolRideTest::Flags)

bool FKiteSurfSchoolRideA5::RunTest(const FString& Parameters)
{
	{
		FLessonRun Run(TEXT("A5"), TEXT("A5good"));
		if (!TestTrue(TEXT("A5 begins"), Run.IsValid()))
		{
			return false;
		}
		TestNearlyEqual(TEXT("Player default: the bar starts at the middle"), Run.StartBar, 0.5f, 1e-3f);
		FTransitioner Rider;
		Rider.Tacker.LegSeconds = 8.0f;
		const ELessonOutcome Outcome = Run.Ride(150.0f, false, [&](FLessonRun& R) { Rider.Step(R); });
		TestEqual(TEXT("Three transitions pass A5"), Outcome, ELessonOutcome::Passed);
		AddInfo(FString::Printf(TEXT("A5: the last turn kept %.2f of the speed"), Run.Director()->GetObjectiveValue()));
	}
	{
		FLessonRun Run(TEXT("A5"), TEXT("A5bad"));
		FTransitioner Rider;
		Rider.Tacker.LegSeconds = 8.0f;
		Rider.Tacker.CrossSteer = 0.25f;
		Rider.Tacker.DiveToClockDeg = 15.0f;
		const ELessonOutcome Outcome = Run.Ride(90.0f, true, [&](FLessonRun& R) { Rider.Step(R); });
		TestEqual(TEXT("A slow kite through the turn fails a transition"), Outcome, ELessonOutcome::AttemptFailed);
		TestEqual(TEXT("with the line"), Run.Director()->GetLastFaultLine().ToString(), FString(TEXT("Steer the kite faster through the turn")));
	}
	return true;
}

// Lesson A7, carving transition: left unpassable on today's physics (below). The kite flown across early
// with the board only following it fails with "The kite left the power too soon".
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolRideA7, "KiteSurf.School.RideA7", SchoolRideTest::Flags)

bool FKiteSurfSchoolRideA7::RunTest(const FString& Parameters)
{
	// Left unpassable on today's physics (docs/tutorials.md 2.1): the best carving transition found keeps the kite's
	// timing in A7's band but is off the plane for about 2.5 s of the 4 s around the tack change (the pass asks for
	// at most 0.1 s). The test rides it and records what it measures, so a physics change that makes it passable shows.
	{
		FLessonRun Run(TEXT("A7"), TEXT("A7good"));
		if (!TestTrue(TEXT("A7 begins"), Run.IsValid()))
		{
			return false;
		}
		TestNearlyEqual(TEXT("Player default: the bar starts at the middle"), Run.StartBar, 0.5f, 1e-3f);
		FTacker Tacker;
		Tacker.LegSeconds = 8.0f;
		Tacker.CarveEdge = 0.5f;
		Run.Ride(90.0f, false, [&](FLessonRun& R) { Tacker.Step(R); });
		const FLessonTelemetry& Tel = Run.Director()->GetTelemetry();
		int32 Judged = 0;
		float BestOffPlane = UE_BIG_NUMBER;
		FString Measured;
		for (const float Change : Tel.FindTackChanges())
		{
			FLessonMeasure M;
			M.Anchor = ELessonAnchor::Event;
			float OffPlane = 0.0f;
			float Lead = 0.0f;
			M.Metric = ELessonMetric::TransitionNotPlaningSeconds;
			if (!LessonEval::ReadMeasure(M, Tel, nullptr, FLessonJumpExtras(), Change, OffPlane))
			{
				continue;
			}
			M.Metric = ELessonMetric::KiteLeadAtTransition;
			const bool bLead = LessonEval::ReadMeasure(M, Tel, nullptr, FLessonJumpExtras(), Change, Lead);
			++Judged;
			BestOffPlane = FMath::Min(BestOffPlane, OffPlane);
			Measured += FString::Printf(TEXT(" [off the plane %.2f s, kite lead %s]"), OffPlane, bLead ? *FString::Printf(TEXT("%.2f s"), Lead) : TEXT("none"));
		}
		AddInfo(FString::Printf(TEXT("A7 carving transitions:%s; pass needs at most 0.1 s off the plane"), *Measured));
		TestTrue(TEXT("The carving rider turns: transitions are judged"), Judged >= 2);
		TestTrue(FString::Printf(TEXT("and gets back on the plane after each (best %.2f s off it in the 4 s around the change)"), BestOffPlane), BestOffPlane < 4.0f);
	}
	{
		// The kite flown across early and the board only following it: "The kite left the power too soon".
		FLessonRun Run(TEXT("A7"), TEXT("A7bad"));
		FTacker Tacker;
		Tacker.LegSeconds = 8.0f;
		const ELessonOutcome Outcome = Run.Ride(90.0f, true, [&](FLessonRun& R) { Tacker.Step(R); });
		TestEqual(TEXT("The kite across long before the board fails"), Outcome, ELessonOutcome::AttemptFailed);
		TestEqual(TEXT("with the line"), Run.Director()->GetLastFaultLine().ToString(), FString(TEXT("The kite left the power too soon")));
	}
	return true;
}

namespace SchoolRideTest
{
	/**
	 * A jumping rider for chapter B, the send-and-pop of SchoolDirectorTests.cpp's FJumpRider through the
	 * player's bar: ride a few seconds with the kite low on the tack's side and the edge held, send it to 12
	 * with the bar part out and the edge loaded, and pop as it reaches the top (or, for a pop with the board,
	 * without sending); in the air hold the kite at 12 and crouch on the way down.
	 */
	struct FJumper
	{
		enum class EPhase { Ride, Send, Air };
		EPhase Phase = EPhase::Ride;
		float PhaseSeconds = 0.0f;
		float AirSeconds = 0.0f;
		float Side = 1.0f;
		float RideSeconds = 5.0f;
		float RideClockDeg = 60.0f;
		float RideBar = 0.7f;
		float Edge = 0.8f;
		/** Send the kite to 12 before the pop; false pops with the kite where it rides (lesson B1). */
		bool bSend = true;
		float SendBar = 0.6f;
		float SendGainDeg = 25.0f;
		float SendMaxSteer = 0.8f;
		/** Pop once the kite is at the top: over TopDeg, or over TopDeg - TopBandDeg and no longer climbing. */
		float TopDeg = 75.0f;
		float TopBandDeg = 5.0f;
		float MaxSendSeconds = 8.0f;
		/** Without a send: load this long before the pop (s). */
		float LoadSeconds = 0.6f;
		float PopBar = 0.8f;
		/** The bar comes in this long before the pop (s). */
		float PopDelaySeconds = 0.15f;
		float TopSeconds = 0.0f;
		/** The B2 mistake: the bar right in once the kite passes this while still climbing. */
		float BadSheetInDeg = -1.0f;
		/** In the air: dive the kite this long before the expected touchdown (s; 0 keeps it at 12) (B3). */
		float DiveLeadSeconds = 0.0f;
		float DiveSteer = 0.8f;
		/** In the air: fly the kite across to the other side for a jump transition (B5); land riding the other way. */
		bool bTransition = false;
		float TransitionSteer = 0.6f;
		/** With bTransition: send on through 12 and pop once the kite is this far round on the new side (deg; above 90 pops at the top as usual). */
		float PopAcrossClockDeg = 999.0f;
		/** ...and only once slower than this (m/s): kill the speed before the pop. */
		float PopBelowSpeedMS = 99.0f;
		int32 Jumps = 0;
		/** While this step index runs, hold the send (B2 step 1 asks only for the kite at 12). */
		int32 HoldSendOnStep = -1;
		bool bHeldSend = false;

		void Step(FLessonRun& Run)
		{
			FHands& H = Run.H;
			PhaseSeconds += RideFrameSeconds;
			const bool bAir = H.InAir();
			switch (Phase)
			{
			case EPhase::Ride:
				H.Bar(RideBar);
				H.SteerToClock(Side * RideClockDeg, 30.0f, 0.5f, 0.3f);
				H.Edge(Side * Edge);
				H.Board()->SetWeightShift(0.0f);
				if (!bSend && PhaseSeconds >= RideSeconds - LoadSeconds)
				{
					H.Load(true);
				}
				if (PhaseSeconds >= RideSeconds && !bAir && !H.Board()->IsCrashing())
				{
					if (bSend)
					{
						Phase = EPhase::Send;
						PhaseSeconds = 0.0f;
					}
					else
					{
						Pop(Run);
					}
				}
				break;
			case EPhase::Send:
			{
				H.SteerToClock(0.0f, SendGainDeg, SendMaxSteer);
				H.Edge(Side * Edge);
				const bool bHold = HoldSendOnStep >= 0 && Run.Director()->GetPhase() == ELessonPhase::Step && Run.Director()->GetStepIndex() == HoldSendOnStep;
				if (bHold)
				{
					H.Bar(SendBar);
					H.Load(false);
					bHeldSend = true;
					break;
				}
				if (bHeldSend)
				{
					// The step that asked only for the kite at 12 is done: ride again and start a send from low down.
					bHeldSend = false;
					Phase = EPhase::Ride;
					PhaseSeconds = 0.0f;
					break;
				}
				H.Bar(BadSheetInDeg > 0.0f && H.Elevation() >= BadSheetInDeg ? 1.0f : SendBar);
				H.Board()->SetWeightShift(-1.0f);
				H.Load(true);
				const float E = H.Elevation();
				bool bTop = E >= TopDeg || (E >= TopDeg - TopBandDeg && H.ElevationRate <= 2.0f && PhaseSeconds > 1.0f);
				if (bTransition && PopAcrossClockDeg < 90.0f)
				{
					H.Steer(-Side * TransitionSteer);
					bTop = -Side * H.Clock() >= -PopAcrossClockDeg && H.SpeedMS() < PopBelowSpeedMS;
				}
				if (bTop || PhaseSeconds > MaxSendSeconds || (BadSheetInDeg > 0.0f && E >= BadSheetInDeg + 15.0f))
				{
					// The bar in at the top, then the pop a moment later, once the bar is in.
					TopSeconds += RideFrameSeconds;
					H.Bar(PopBar);
					if (TopSeconds >= PopDelaySeconds)
					{
						Pop(Run);
					}
				}
				break;
			}
			case EPhase::Air:
				if (bAir)
				{
					AirSeconds += RideFrameSeconds;
					const float Vz = KiteUnits::CmToM(H.Board()->Velocity.Z);
					const float HeightM = KiteUnits::CmToM(H.Pawn->GetActorLocation().Z - H.Board()->GetWaterSurfaceHeightCm());
					const float ToTouchdown = Vz < -0.1f ? HeightM / -Vz : UE_BIG_NUMBER;
					if (bTransition)
					{
						H.Steer(-Side * TransitionSteer);
					}
					else if (DiveLeadSeconds > 0.0f && Vz < 0.0f && ToTouchdown <= DiveLeadSeconds)
					{
						H.Steer(Side * DiveSteer);
					}
					else
					{
						H.SteerToClock(0.0f, 40.0f, 0.3f);
					}
					if (Vz < 0.0f)
					{
						H.Load(true);
					}
				}
				if ((!bAir && AirSeconds > 0.2f) || PhaseSeconds > 6.0f)
				{
					H.Load(false);
					Phase = EPhase::Ride;
					PhaseSeconds = 0.0f;
					if (bTransition)
					{
						Side = -Side;
					}
				}
				break;
			}
		}

		void Pop(FLessonRun& Run)
		{
			FHands& H = Run.H;
			H.Bar(PopBar);
			H.Pop();
			H.Board()->SetWeightShift(0.0f);
			TopSeconds = 0.0f;
			Phase = EPhase::Air;
			PhaseSeconds = 0.0f;
			AirSeconds = 0.0f;
			++Jumps;
		}
	};

	/**
	 * A bigger jump (B3): ride fast with the bar in and a light edge, send the kite quickly with a full bar and pop
	 * as it passes 45 deg, while the board still has its speed.
	 */
	FJumper HardSender()
	{
		FJumper J;
		J.RideSeconds = 10.0f;
		J.RideBar = 1.0f;
		J.Edge = 0.3f;
		J.RideClockDeg = 55.0f;
		J.SendBar = 0.8f;
		J.PopBar = 1.0f;
		J.SendGainDeg = 15.0f;
		J.SendMaxSteer = 1.0f;
		J.TopDeg = 45.0f;
		J.TopBandDeg = 0.0f;
		J.PopDelaySeconds = 0.0f;
		return J;
	}

	/** The heights and grades of the jumps so far, for the log. */
	FString JumpLog(FLessonRun& Run)
	{
		FJumpRecord Record;
		const UTrickTrackerComponent* Tracker = Run.Fx.Pawn->GetTrickTracker();
		return Tracker && Tracker->GetLastJumpRecord(Record)
			? FString::Printf(TEXT("last jump %.2f m %s %s, kite %.0f at touchdown"), Record.ApexHeightCm / 100.0f, Record.bPopped ? TEXT("popped") : TEXT("lifted"),
				*UEnum::GetValueAsString(Record.Grade), Record.KiteElevationAtLandingDeg)
			: FString(TEXT("no jump"));
	}
}

// Lesson B1, pop: an edge held, then the load and a pop with the kite kept low pass (the kite moves 2 to
// 4 deg around the take-off); a fast send popped while the kite climbs (a kite jump: it rises 11 to 14 deg)
// fails with "You lifted the kite: pop with the board".
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolRideB1, "KiteSurf.School.RideB1", SchoolRideTest::Flags)

bool FKiteSurfSchoolRideB1::RunTest(const FString& Parameters)
{
	{
		FLessonRun Run(TEXT("B1"), TEXT("B1good"));
		if (!TestTrue(TEXT("B1 begins"), Run.IsValid()))
		{
			return false;
		}
		TestNearlyEqual(TEXT("Player default: the bar starts at the middle"), Run.StartBar, 0.5f, 1e-3f);
		FJumper Jumper;
		Jumper.bSend = false;
		Jumper.RideClockDeg = 45.0f;
		const ELessonOutcome Outcome = Run.Ride(150.0f, false, [&](FLessonRun& R) { Jumper.Step(R); });
		TestEqual(TEXT("Pops with the board pass B1"), Outcome, ELessonOutcome::Passed);
		AddInfo(JumpLog(Run));
	}
	{
		FLessonRun Run(TEXT("B1"), TEXT("B1bad"));
		// The kite sent up and the rider popping while it climbs: a kite jump, not a pop.
		FJumper Jumper;
		Jumper.SendGainDeg = 15.0f;
		Jumper.SendMaxSteer = 1.0f;
		Jumper.TopDeg = 45.0f;
		Jumper.TopBandDeg = 0.0f;
		Jumper.PopDelaySeconds = 0.0f;
		const ELessonOutcome Outcome = Run.Ride(90.0f, true, [&](FLessonRun& R) { Jumper.Step(R); });
		TestEqual(TEXT("Lifting the kite through the pop fails"), Outcome, ELessonOutcome::AttemptFailed);
		TestEqual(TEXT("with the line"), Run.Director()->GetLastFaultLine().ToString(), FString(TEXT("You lifted the kite: pop with the board")));
		AddInfo(JumpLog(Run));
	}
	return true;
}

// Lesson B2, small jump: sends with the bar part out and the bar in as the kite reaches the top
// (LessonTiming::SheetPerfectMinDeg, tuned to 75 deg) land five 1 to 2 m jumps Clean in a row and pass,
// with the slow-motion cue and the sheet-in grade agreeing on the moment; the bar in while the kite
// climbs fails with "Bar out while it climbs, in at 12".
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolRideB2, "KiteSurf.School.RideB2", SchoolRideTest::Flags)

bool FKiteSurfSchoolRideB2::RunTest(const FString& Parameters)
{
	{
		FLessonRun Run(TEXT("B2"), TEXT("B2good"));
		if (!TestTrue(TEXT("B2 begins"), Run.IsValid()))
		{
			return false;
		}
		TestNearlyEqual(TEXT("Player default: the bar starts at the middle"), Run.StartBar, 0.5f, 1e-3f);
		FJumper Jumper;
		Jumper.HoldSendOnStep = 0;
		TMap<ELessonTimingGrade, int32> Grades;
		int32 SeenSerial = Run.Director()->GetTimingSerial();
		const ELessonOutcome Outcome = Run.Ride(200.0f, false, [&](FLessonRun& R)
		{
			Jumper.Step(R);
			if (R.Director()->GetTimingSerial() != SeenSerial)
			{
				SeenSerial = R.Director()->GetTimingSerial();
				Grades.FindOrAdd(R.Director()->GetTimingGrade())++;
			}
		});
		TestEqual(TEXT("Sends and pops at the top pass B2"), Outcome, ELessonOutcome::Passed);
		AddInfo(JumpLog(Run));
		AddInfo(FString::Printf(TEXT("B2 sheet-in grades: %d Perfect, %d Good, %d Early, %d Late; %d slow motions"), Grades.FindRef(ELessonTimingGrade::Perfect),
			Grades.FindRef(ELessonTimingGrade::Good), Grades.FindRef(ELessonTimingGrade::Early), Grades.FindRef(ELessonTimingGrade::Late), Run.Director()->GetSlowMotionSerial()));
		// The bar in at the top the cue marks grades Perfect: the cue, the grade and the pass threshold agree.
		TestTrue(TEXT("The slow-motion cue came (the run applies no dilation, the cue still fires)"), Run.Director()->GetSlowMotionSerial() >= 1);
		TestTrue(TEXT("The bar in at the top grades Perfect"), Grades.FindRef(ELessonTimingGrade::Perfect) >= 1);
		TestEqual(TEXT("and never Early"), Grades.FindRef(ELessonTimingGrade::Early), 0);
	}
	{
		FLessonRun Run(TEXT("B2"), TEXT("B2bad"));
		FJumper Jumper;
		Jumper.HoldSendOnStep = 0;
		Jumper.BadSheetInDeg = 45.0f;
		Jumper.PopBar = 1.0f;
		const ELessonOutcome Outcome = Run.Ride(120.0f, true, [&](FLessonRun& R) { Jumper.Step(R); });
		TestEqual(TEXT("The bar in while the kite climbs fails"), Outcome, ELessonOutcome::AttemptFailed);
		TestEqual(TEXT("with the line"), Run.Director()->GetLastFaultLine().ToString(), FString(TEXT("Bar out while it climbs, in at 12")));
		AddInfo(JumpLog(Run));
	}
	return true;
}

// Lesson B3, landing dive: jumps of 1.5 m or more (tuned from 3 m) with the kite at 12 until the last
// second land Clean and pass; a hard, flat send lands with the kite still climbing, never dived, and
// fails with "Dived late: no pull, you sank".
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolRideB3, "KiteSurf.School.RideB3", SchoolRideTest::Flags)

bool FKiteSurfSchoolRideB3::RunTest(const FString& Parameters)
{
	{
		FLessonRun Run(TEXT("B3"), TEXT("B3good"));
		if (!TestTrue(TEXT("B3 begins"), Run.IsValid()))
		{
			return false;
		}
		TestNearlyEqual(TEXT("Player default: the bar starts at the middle"), Run.StartBar, 0.5f, 1e-3f);
		FJumper Jumper;
		Jumper.DiveLeadSeconds = 0.8f;
		const ELessonOutcome Outcome = Run.Ride(200.0f, false, [&](FLessonRun& R) { Jumper.Step(R); });
		TestEqual(TEXT("Jumps with a late dive pass B3"), Outcome, ELessonOutcome::Passed);
		AddInfo(JumpLog(Run));
	}
	{
		FLessonRun Run(TEXT("B3"), TEXT("B3bad"));
		// A hard, flat send popped with the kite at 45 deg: it is still climbing at touchdown, never dived.
		FJumper Jumper = HardSender();
		const ELessonOutcome Outcome = Run.Ride(120.0f, true, [&](FLessonRun& R) { Jumper.Step(R); });
		TestEqual(TEXT("Landing with the kite still climbing fails"), Outcome, ELessonOutcome::AttemptFailed);
		TestEqual(TEXT("with the line"), Run.Director()->GetLastFaultLine().ToString(), FString(TEXT("Dived late: no pull, you sank")));
		AddInfo(JumpLog(Run));
	}
	return true;
}

// Lesson B5, jump transition: the speed killed at the top, a pop with the kite just past 12 and the kite
// flown on across in the air land the rider going the other way and pass; a hard send popped at speed
// (over 2.5 m/s, tuned from 10) fails with "Pop harder to kill your speed".
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolRideB5, "KiteSurf.School.RideB5", SchoolRideTest::Flags)

bool FKiteSurfSchoolRideB5::RunTest(const FString& Parameters)
{
	{
		FLessonRun Run(TEXT("B5"), TEXT("B5good"));
		if (!TestTrue(TEXT("B5 begins"), Run.IsValid()))
		{
			return false;
		}
		TestNearlyEqual(TEXT("Player default: the bar starts at the middle"), Run.StartBar, 0.5f, 1e-3f);
		FJumper Jumper;
		Jumper.bTransition = true;
		// Wait with the kite at 12 for the speed to die, pop with it just past 12 and steer it on across in the air.
		Jumper.TopDeg = 999.0f;
		Jumper.PopAcrossClockDeg = 0.0f;
		Jumper.PopBelowSpeedMS = 1.0f;
		Jumper.TransitionSteer = 0.35f;
		Jumper.MaxSendSeconds = 12.0f;
		const ELessonOutcome Outcome = Run.Ride(150.0f, false, [&](FLessonRun& R) { Jumper.Step(R); });
		TestEqual(TEXT("A jump transition passes B5"), Outcome, ELessonOutcome::Passed);
		AddInfo(JumpLog(Run));
	}
	{
		FLessonRun Run(TEXT("B5"), TEXT("B5bad"));
		FJumper Jumper = HardSender();
		Jumper.bTransition = true;
		Jumper.TransitionSteer = 0.35f;
		const ELessonOutcome Outcome = Run.Ride(120.0f, true, [&](FLessonRun& R) { Jumper.Step(R); });
		TestEqual(TEXT("Taking off too fast fails"), Outcome, ELessonOutcome::AttemptFailed);
		TestEqual(TEXT("with the line"), Run.Director()->GetLastFaultLine().ToString(), FString(TEXT("Pop harder to kill your speed")));
		AddInfo(JumpLog(Run));
	}
	return true;
}

namespace SchoolRideTest
{
	/** B6: ride on the tack, fly the kite over the top to the other side and keep the bar that way: it loops down through the window and pulls the rider round. */
	struct FDownlooper
	{
		enum class EPhase { Ride, Up, Loop };
		EPhase Phase = EPhase::Ride;
		float PhaseSeconds = 0.0f;
		float Side = 1.0f;
		float RideSeconds = 6.0f;
		/** Let go of the bar this long into the loop (s; 0 holds it until the kite climbs). */
		float LetGoAfterSeconds = 0.0f;
		/** Fly the kite up to this elevation before the loop (deg; 0 loops from where it rides). */
		float UpFirstDeg = 0.0f;
		float LoopBar = 0.85f;

		void Step(FLessonRun& Run)
		{
			FHands& H = Run.H;
			PhaseSeconds += RideFrameSeconds;
			switch (Phase)
			{
			case EPhase::Ride:
				H.Bar(0.85f);
				H.SteerToClock(Side * 50.0f, 40.0f, 0.4f, 0.3f);
				H.HoldCourse(-10.0f);
				if (PhaseSeconds > RideSeconds)
				{
					Phase = UpFirstDeg > 0.0f ? EPhase::Up : EPhase::Loop;
					PhaseSeconds = 0.0f;
				}
				break;
			case EPhase::Up:
				H.Bar(0.6f);
				H.SteerToClock(Side * 15.0f, 25.0f, 0.6f, 0.3f);
				H.Edge(0.0f);
				if (H.Elevation() >= UpFirstDeg || PhaseSeconds > 8.0f)
				{
					Phase = EPhase::Loop;
					PhaseSeconds = 0.0f;
				}
				break;
			case EPhase::Loop:
			{
				H.Bar(LoopBar);
				H.Edge(0.0f);
				const bool bLetGo = LetGoAfterSeconds > 0.0f && H.Kite()->IsLooping() && PhaseSeconds > LetGoAfterSeconds;
				H.Steer(bLetGo ? 0.0f : -Side);
				// Done once the kite is back up on the new side after its loop.
				if (PhaseSeconds > 3.0f && -Side * H.Clock() > 20.0f && H.Elevation() > 30.0f && H.ElevationRate >= 0.0f && !H.Kite()->IsLooping())
				{
					Side = -Side;
					Phase = EPhase::Ride;
					PhaseSeconds = 0.0f;
				}
				else if (PhaseSeconds > 12.0f)
				{
					Phase = EPhase::Ride;
					PhaseSeconds = 0.0f;
				}
				break;
			}
			}
		}
	};
}

// Lesson B6, downloop transition (tuned: 14 kn on the loop kite): the kite flown up, over the top and
// looped down through the window on the new side pulls the rider round onto the other tack and passes;
// letting go mid-loop fails with "Keep steering until the kite climbs".
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolRideB6, "KiteSurf.School.RideB6", SchoolRideTest::Flags)

bool FKiteSurfSchoolRideB6::RunTest(const FString& Parameters)
{
	{
		FLessonRun Run(TEXT("B6"), TEXT("B6good"));
		if (!TestTrue(TEXT("B6 begins"), Run.IsValid()))
		{
			return false;
		}
		TestNearlyEqual(TEXT("Player default: the bar starts at the middle"), Run.StartBar, 0.5f, 1e-3f);
		FDownlooper Looper;
		Looper.UpFirstDeg = 65.0f;
		const ELessonOutcome Outcome = Run.Ride(120.0f, false, [&](FLessonRun& R) { Looper.Step(R); });
		TestEqual(TEXT("A downloop transition passes B6"), Outcome, ELessonOutcome::Passed);
	}
	{
		FLessonRun Run(TEXT("B6"), TEXT("B6bad"));
		FDownlooper Looper;
		Looper.UpFirstDeg = 65.0f;
		Looper.LetGoAfterSeconds = 0.5f;
		const ELessonOutcome Outcome = Run.Ride(90.0f, true, [&](FLessonRun& R) { Looper.Step(R); });
		TestEqual(TEXT("Letting go mid-loop fails"), Outcome, ELessonOutcome::AttemptFailed);
		TestEqual(TEXT("with the line"), Run.Director()->GetLastFaultLine().ToString(), FString(TEXT("Keep steering until the kite climbs")));
	}
	return true;
}


// ---------------------------------------------------------------------------------------------
// The evaluator rules the rides needed (docs/tutorials.md 2.1), on synthetic telemetry.
// ---------------------------------------------------------------------------------------------

namespace SchoolRideTest
{
	FLessonSample SyntheticSample(float Time, EBoardState State, int32 Tack, float KiteDeg = 45.0f, float Bar = 0.5f)
	{
		FLessonSample S;
		S.TimeSeconds = Time;
		S.BoardState = State;
		S.SpeedMS = State == EBoardState::Planing ? 7.0f : 2.0f;
		S.Tack = Tack;
		S.KiteElevationDeg = KiteDeg;
		S.BarPosition = Bar;
		return S;
	}
}

// One "kite at the top" for B2: the slow-motion cue, the sheet-in grade and the sheeted-in-while-climbing
// rule agree. A loaded send that stops climbing at 72 deg is at the top: the cue fires there, the bar in
// there grades Perfect and is not "sheeted in while climbing". The bar in on a kite still climbing
// through 72 deg is Good, and the rule sees it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolKiteTopAgrees, "KiteSurf.School.KiteTopAgrees", SchoolRideTest::Flags)

bool FKiteSurfSchoolKiteTopAgrees::RunTest(const FString& Parameters)
{
	const FLessonDef* B2 = LessonCatalog::Find(TEXT("B2"));
	if (!TestNotNull(TEXT("B2"), B2))
	{
		return false;
	}
	TestEqual(TEXT("B2 step 1 asks for the kite at the top"), B2->Steps[0].Objective.Min, LessonTiming::SheetPerfectMinDeg);
	TestEqual(TEXT("B2 step 2's slow motion fires at the top"), B2->Steps[1].SlowMo.Threshold, LessonTiming::SheetPerfectMinDeg);
	TestEqual(TEXT("The climbing rule's ceiling is the top"), LessonTelemetry::ClimbCeilingDeg, LessonTiming::SheetPerfectMinDeg);
	const FLessonFault* Sheeted = B2->Faults.FindByPredicate([](const FLessonFault& F) { return F.Id == FName(TEXT("SheetedInClimbing")); });
	if (!TestNotNull(TEXT("B2's sheeted-in-while-climbing rule"), Sheeted))
	{
		return false;
	}

	// Kite climbing at 12 deg/s from 60 to 72 deg, then stopped there; Bar in at BarInAt (s).
	auto Send = [&](float StopDeg, float BarInAt, ELessonTimingGrade& OutGrade, float& OutCueTime, float& OutBarWhileClimbing)
	{
		FLessonTelemetry Tel;
		LessonTiming::FSlowMoArm Arm;
		OutGrade = ELessonTimingGrade::None;
		OutCueTime = -1.0f;
		for (int32 I = 0; I <= 240; ++I)
		{
			const float T = I / 60.0f;
			const float Deg = FMath::Min(60.0f + 12.0f * T, StopDeg);
			const FLessonSample S = SyntheticSample(T, EBoardState::Planing, 1, Deg, T >= BarInAt ? 0.9f : 0.5f);
			Tel.Add(S);
			if (OutCueTime < 0.0f && LessonTiming::DetectSlowMoCue(B2->Steps[1].SlowMo, S, Arm))
			{
				OutCueTime = T;
			}
			ELessonTimingGrade Grade = ELessonTimingGrade::None;
			if (LessonTiming::DetectSheetIn(Tel, Grade))
			{
				OutGrade = Grade;
			}
		}
		FJumpRecord Jump;
		Jump.TakeoffTimeSeconds = BarInAt + 0.15f;
		OutBarWhileClimbing = -1.0f;
		LessonEval::ReadMeasure(Sheeted->Measure, Tel, &Jump, FLessonJumpExtras(), Jump.LandingTimeSeconds, OutBarWhileClimbing);
	};

	ELessonTimingGrade Grade = ELessonTimingGrade::None;
	float CueTime = 0.0f;
	float BarWhileClimbing = 0.0f;
	// Stops at 72 deg at 1.0 s; the cue fires once it has stopped (the next sample), the bar comes in then.
	Send(72.0f, 1.05f, Grade, CueTime, BarWhileClimbing);
	TestTrue(FString::Printf(TEXT("The cue fires when the kite stops at 72 deg (at %.2f s)"), CueTime), CueTime > 0.99f && CueTime < 1.1f);
	TestEqual(TEXT("The bar in there grades Perfect"), Grade, ELessonTimingGrade::Perfect);
	TestTrue(FString::Printf(TEXT("and is not sheeting in while the kite climbs (%.2f)"), BarWhileClimbing), BarWhileClimbing >= 0.0f && BarWhileClimbing <= LessonRules::SheetedInBar);

	// Still climbing through 72 deg (on to 85): no cue yet at 72, the bar in grades Good, and the rule sees it.
	Send(85.0f, 1.0f, Grade, CueTime, BarWhileClimbing);
	TestTrue(FString::Printf(TEXT("No cue while it climbs through 72 deg, nor once the bar is in (first at %.2f s)"), CueTime), CueTime < 0.0f);
	TestEqual(TEXT("The bar in at 72 deg while it climbs grades Good"), Grade, ELessonTimingGrade::Good);
	TestTrue(FString::Printf(TEXT("and is sheeting in while it climbs (%.2f)"), BarWhileClimbing), BarWhileClimbing > LessonRules::SheetedInBar);
	return true;
}

// A rule read at a ride event (a tack change) needs a recent one when no event is being judged: a
// held objective's hint long after a turn does not blame the turn. With the judged event given, it reads it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolFaultIgnoresStaleTackChange, "KiteSurf.School.FaultIgnoresStaleTackChange", SchoolRideTest::Flags)

bool FKiteSurfSchoolFaultIgnoresStaleTackChange::RunTest(const FString& Parameters)
{
	const FLessonDef* A5 = LessonCatalog::Find(TEXT("A5"));
	if (!TestNotNull(TEXT("A5"), A5))
	{
		return false;
	}
	// Riding on tack +1, a turn at 10 s after which the rider sank (not planing for 3 s), then planing again.
	auto Ride = [](float Until)
	{
		FLessonTelemetry Tel;
		for (int32 I = 0; I / 60.0f <= Until; ++I)
		{
			const float T = I / 60.0f;
			const bool bSank = T >= 10.0f && T < 13.0f;
			Tel.Add(SyntheticSample(T, bSank ? EBoardState::Displacement : EBoardState::Planing, T < 10.0f ? 1 : -1));
		}
		return Tel;
	};
	const FLessonFault* SankAfter = A5->Faults.FindByPredicate([](const FLessonFault& F) { return F.Id == FName(TEXT("SankAfter")); });
	if (!TestNotNull(TEXT("A5's sank-after rule"), SankAfter) || !TestTrue(TEXT("It reads the Event anchor"), LessonEval::ReadsEvent(SankAfter->Measure)))
	{
		return false;
	}
	const TArray<FLessonFault> Rules = { *SankAfter };
	const FLessonTelemetry Soon = Ride(12.5f);
	const FLessonTelemetry Late = Ride(25.0f);
	TestNotNull(TEXT("2.5 s after the turn, no event judged: the turn is recent and the rule reads it"), LessonEval::DiagnoseFault(Rules, Soon, FJumpRecord()));
	TestNull(TEXT("15 s after it: a stale turn is not read"), LessonEval::DiagnoseFault(Rules, Late, FJumpRecord()));
	TestNotNull(TEXT("15 s after it with the turn as the judged event: read"), LessonEval::DiagnoseFault(Rules, Late, FJumpRecord(), FLessonJumpExtras(), 10.0f));
	// The HUD's hint for a held objective (A5 step 1, slow down) uses the same diagnosis: nothing to blame on the old turn.
	TestEqual(TEXT("A held objective's hint 15 s after the turn is not the turn's line"),
		LessonHUD::HeldHintLine(A5->Faults, A5->Steps[0].Objective, Late, 7.0f) == SankAfter->Feedback.ToString(), false);
	return true;
}

// UpwindGain and DistanceRidden: a leg the rider planed in and then came off the plane for
// LessonEval::LegLostSeconds is a missed attempt (so its fault line can show); a tack change starts a
// new leg on a bEachTack objective, so a turn off the plane is not a miss.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolLegLostIsAMiss, "KiteSurf.School.LegLostIsAMiss", SchoolRideTest::Flags)

bool FKiteSurfSchoolLegLostIsAMiss::RunTest(const FString& Parameters)
{
	auto Evaluate = [](const FLessonObjective& Objective, bool bTurn)
	{
		FLessonTelemetry Tel;
		for (int32 I = 0; I / 60.0f <= 8.0f; ++I)
		{
			const float T = I / 60.0f;
			FLessonSample S = SyntheticSample(T, T < 5.0f ? EBoardState::Planing : EBoardState::Displacement, bTurn && T >= 5.5f ? -1 : 1);
			S.UpwindM = T < 5.0f ? 1.0f * T : 5.0f;
			Tel.Add(S);
		}
		FLessonProgress P;
		return LessonEval::EvaluateObjective(Objective, P, Tel, nullptr);
	};
	FLessonObjective Upwind;
	Upwind.Metric = ELessonMetric::UpwindGain;
	Upwind.Min = 50.0f;
	const FObjectiveResult Lost = Evaluate(Upwind, false);
	TestTrue(TEXT("Planed 5 s, then off the plane 3 s: the leg is a missed attempt"), Lost.bFailed && Lost.NewProgress.Failures == 1);
	TestTrue(TEXT("and the next leg is measured from now"), FMath::IsNearlyEqual(Lost.NewProgress.ReferenceTimeSeconds, 8.0f, 0.02f));
	FLessonObjective Distance = Upwind;
	Distance.Metric = ELessonMetric::DistanceRidden;
	TestTrue(TEXT("The same for a distance"), Evaluate(Distance, false).bFailed);
	FLessonObjective EachTack = Upwind;
	EachTack.Count = 2;
	EachTack.bEachTack = true;
	TestFalse(TEXT("Off the plane through a turn (bEachTack): a new leg, not a miss"), Evaluate(EachTack, true).bFailed);
	return true;
}

#endif
