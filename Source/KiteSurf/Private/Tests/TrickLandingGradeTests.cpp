#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "BoardMovementComponent.h"
#include "KiteComponent.h"
#include "KiteRiderPawn.h"
#include "KiteSurf.h"
#include "KiteSurfGameMode.h"
#include "KiteSurfUnits.h"
#include "Tricks/LandingEvaluator.h"
#include "WindComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// Batch C (docs/tricks/review.md section 4): landing grades that match the jump. Too hard is now
// read from the landing g alone (TrickLandingTests.cpp's existing pure-evaluator tests cover the
// rule directly); these acceptance tests fly the real 30 kn timed jump and the 60 kn storm jump
// through the whole pawn, the way docs/jumping.md documents them, to prove the grade a player
// actually sees matches.

namespace TrickLandingGradeTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float RideDeltaTime = 1.0f / 60.0f;
	constexpr float KnotCmS = 51.44f;

	/**
	 * The timed big-air jump (docs/jumping.md "Hang time"): ride a few seconds, send the kite hard
	 * up with the weight on the tail and the edge loaded, let go to pop ReleaseSeconds after the
	 * kite starts to answer, then crouch on the way down so the airborne assist holds the kite
	 * overhead. With DiveLeadSeconds > 0, in the last DiveLeadSeconds estimated before touchdown
	 * (height over sink rate) the bar goes to DiveSteer instead of staying centred: the B3 lesson's
	 * dive (SchoolLessonRideTests.cpp's FJumper::DiveLeadSeconds), steering the kite down and
	 * forward to catch the rider instead of holding it overhead all the way down.
	 */
	struct FGradeJump
	{
		bool bCameDown = false;
		FLandingVerdict Verdict;
		float LandingG = 0.0f;
		float LandingSinkMS = 0.0f;
		float LandingKiteElevationDeg = 0.0f;
		float PeakCm = 0.0f;
		float AirSeconds = 0.0f;
	};

	FGradeJump RunTimedJump(float WindKnots, float ReleaseSeconds, float DiveLeadSeconds = 0.0f, float DiveSteer = 1.0f)
	{
		FGradeJump Result;
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		AKiteRiderPawn* Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
		if (!Pawn)
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
			return Result;
		}
		UKiteComponent* Kite = Pawn->GetKite();
		UBoardMovementComponent* Board = Pawn->GetBoardMovement();
		if (UWindComponent* Wind = Pawn->GetWind())
		{
			Wind->BaseWind = FVector(WindKnots * KnotCmS, 0.0f, 0.0f);
			Wind->GustStrength = 0.0f;
			Wind->DirectionDriftDeg = 0.0f;
		}
		AKiteSurfGameMode::InitializeRide(Pawn, 12.0f * KnotCmS, 1.0f);
		// As FRideFixture (RideLoopTests.cpp): hold the kite where the start put it, as a rider's
		// hands would, rather than letting it drift to 12 with the bar centred during the warm-up.
		Kite->bParkHoldAssist = true;
		Kite->SetKiteModel(EKiteModel::Loop);
		Kite->SetKiteSize(UKiteComponent::RecommendKiteSizeM2(WindKnots));

		auto Simulate = [&](float Seconds)
		{
			for (int32 Step = 0, Count = FMath::RoundToInt(Seconds / RideDeltaTime); Step < Count; ++Step)
			{
				Pawn->Tick(RideDeltaTime);
			}
		};
		Simulate(8.0f);

		Pawn->SteerKite(-1.0f);
		const float SendDeadTimeSeconds = Kite->GetSteeringDeadTimeSeconds();
		Board->SetWeightShift(-1.0f);
		Pawn->SetLoadHeld(true);

		bool bLeftWater = false;
		for (float Elapsed = 0.0f; Elapsed < 60.0f; Elapsed += RideDeltaTime)
		{
			Simulate(RideDeltaTime);
			const bool bAir = Board->GetBoardState() == EBoardState::Airborne;
			if (bAir && !bLeftWater)
			{
				// The kite pulled the rider off their edge before the timed release.
				bLeftWater = true;
				Board->SetWeightShift(0.0f);
				Pawn->SetLoadHeld(false);
				Pawn->SheetKite(1.0f);
			}
			if (!bLeftWater && Elapsed >= ReleaseSeconds + SendDeadTimeSeconds)
			{
				Board->SetWeightShift(-1.0f);
				Pawn->SheetKite(1.0f);
				Pawn->ReleaseLoadAndPop();
				Board->SetWeightShift(0.0f);
				bLeftWater = true;
			}
			if (bAir)
			{
				Result.AirSeconds += RideDeltaTime;
				Result.PeakCm = FMath::Max(Result.PeakCm, Board->GetCurrentJumpHeight());
				// Centred as soon as airborne (as RunJump, RideLoopTests.cpp): the assist then flies
				// the kite overhead and holds it there all the way down, unless the last-second dive
				// below overrides it.
				bool bDiving = false;
				if (Board->Velocity.Z < 0.0f)
				{
					Pawn->SetLoadHeld(true); // crouch on the way down: doubles the absorb distance
					const float HeightM = Board->GetCurrentJumpHeight() / 100.0f;
					const float SinkMS = -Board->Velocity.Z / 100.0f;
					const float SecondsToTouchdown = SinkMS > 0.01f ? HeightM / SinkMS : UE_BIG_NUMBER;
					bDiving = DiveLeadSeconds > 0.0f && SecondsToTouchdown <= DiveLeadSeconds;
				}
				Pawn->SteerKite(bDiving ? DiveSteer : 0.0f); // the dive: catch the rider instead of holding overhead
			}
			else if (bLeftWater && Result.AirSeconds > 0.3f)
			{
				Result.bCameDown = true;
				Result.Verdict = Board->GetLastLandingVerdict();
				Result.LandingG = Board->GetLastLandingG();
				Result.LandingSinkMS = Board->GetLastLandingSinkMS();
				Result.LandingKiteElevationDeg = Kite->GetElevationDeg();
				break;
			}
		}
		Pawn->SetLoadHeld(false);
		World->DestroyWorld(false);
		return Result;
	}

	FString GradeName(ELandingGrade Grade)
	{
		return StaticEnum<ELandingGrade>()->GetNameStringByValue(static_cast<int64>(Grade));
	}

	FString CauseName(ELandingCause Cause)
	{
		return StaticEnum<ELandingCause>()->GetNameStringByValue(static_cast<int64>(Cause));
	}
}

// The timed jump at 30 kn, crouched for the landing, no dive: lands at a g the crouch has already
// taken most of the sting out of, the kite still well above 45 deg. Before batch C it graded
// sketchy, too hard, from its raw sink alone (docs/jumping.md "The timed jump at 30 kn").
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickCrouchedBigAirLandsClean, "KiteSurf.Trick.CrouchedBigAirLandsClean", TrickLandingGradeTest::Flags)

bool FKiteSurfTrickCrouchedBigAirLandsClean::RunTest(const FString& Parameters)
{
	using namespace TrickLandingGradeTest;
	const FGradeJump Jump = RunTimedJump(30.0f, 0.66f);
	TestTrue(TEXT("The rider came back down"), Jump.bCameDown);
	if (!Jump.bCameDown)
	{
		return false;
	}
	UE_LOG(LogKiteSurf, Log, TEXT("CrouchedBigAirLandsClean: %.1f m, %.2f s in the air, landed sinking %.2f m/s with the kite %.1f deg up: %.2f g, %s%s"),
		Jump.PeakCm / 100.0f, Jump.AirSeconds, Jump.LandingSinkMS, Jump.LandingKiteElevationDeg, Jump.LandingG,
		*GradeName(Jump.Verdict.Grade), Jump.Verdict.Cause == ELandingCause::None ? TEXT("") : *(TEXT(", ") + CauseName(Jump.Verdict.Cause)));

	TestTrue(FString::Printf(TEXT("A real big air (%.1f m)"), Jump.PeakCm / 100.0f), Jump.PeakCm > 500.0f);
	TestTrue(FString::Printf(TEXT("The kite is above 45 deg at touchdown (%.1f deg)"), Jump.LandingKiteElevationDeg), Jump.LandingKiteElevationDeg > 45.0f);
	TestEqual(FString::Printf(TEXT("Graded clean, not sketchy (landing %.2f g)"), Jump.LandingG), Jump.Verdict.Grade, ELandingGrade::Clean);
	TestEqual(TEXT("with no cause"), Jump.Verdict.Cause, ELandingCause::None);
	return true;
}

// The 60 kn storm jump (docs/jumping.md "In a storm the kite loads up at once"): released at 0.2 s,
// it lands at about 9.4 g, over SketchyMinLandingG but under CrashLandingG, so it stays sketchy, too
// hard, exactly as before batch C: a landing this hard is too hard whichever rule reads the g.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickStormLandingStaysHot, "KiteSurf.Trick.StormLandingStaysHot", TrickLandingGradeTest::Flags)

bool FKiteSurfTrickStormLandingStaysHot::RunTest(const FString& Parameters)
{
	using namespace TrickLandingGradeTest;
	const FGradeJump Jump = RunTimedJump(60.0f, 0.2f);
	TestTrue(TEXT("The rider came back down"), Jump.bCameDown);
	if (!Jump.bCameDown)
	{
		return false;
	}
	UE_LOG(LogKiteSurf, Log, TEXT("StormLandingStaysHot: %.1f m, %.2f s in the air, landed sinking %.2f m/s with the kite %.1f deg up: %.2f g, %s%s"),
		Jump.PeakCm / 100.0f, Jump.AirSeconds, Jump.LandingSinkMS, Jump.LandingKiteElevationDeg, Jump.LandingG,
		*GradeName(Jump.Verdict.Grade), Jump.Verdict.Cause == ELandingCause::None ? TEXT("") : *(TEXT(", ") + CauseName(Jump.Verdict.Cause)));

	TestTrue(FString::Printf(TEXT("A storm jump, higher than the 30 kn one (%.1f m)"), Jump.PeakCm / 100.0f), Jump.PeakCm > 1500.0f);
	TestTrue(FString::Printf(TEXT("Over SketchyMinLandingG but under CrashLandingG (%.2f g)"), Jump.LandingG), Jump.LandingG > 8.0f && Jump.LandingG < 10.0f);
	TestEqual(FString::Printf(TEXT("Still graded sketchy at %.2f g"), Jump.LandingG), Jump.Verdict.Grade, ELandingGrade::Sketchy);
	TestEqual(TEXT("cause too hard"), Jump.Verdict.Cause, ELandingCause::TooHard);
	return true;
}

// The B3 lesson's dive (docs/tutorials.md, LessonCatalog.cpp "B3"), flown on the big 30 kn jump
// instead of B3's small one: steering the kite down and forward in the last second instead of
// holding it overhead trades some of the kite's overhead hold for a last-moment catch, landing
// softer than the undived crouch.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickDivedBigAirStomps, "KiteSurf.Trick.DivedBigAirStomps", TrickLandingGradeTest::Flags)

bool FKiteSurfTrickDivedBigAirStomps::RunTest(const FString& Parameters)
{
	using namespace TrickLandingGradeTest;
	const FGradeJump Undived = RunTimedJump(30.0f, 0.66f);
	// 1.8 s out, a steer of 0.7: steering the kite down and forward that early (B3's own dive is
	// much shorter, but B3's jumps are a fifth the height and a third the airtime) trades some of
	// the overhead hold for a last-moment catch, slowing the sink from 8.17 to 6.66 m/s. Found by a
	// sweep (not committed) of lead times 0.2 to 2.6 s and steers 0.3 to 1.0 either way: under about
	// 1.2 s the dive starts and ends too fast to add anything (the kite stays overhead regardless,
	// at the same g as undived); past 1.2 s a steer under about 0.6 softens only a little, a full
	// 1.0 swings the kite past vertical and loses more lift than it gains, so it lands harder than
	// undived; this combination is inside the window that works, not the edge of it.
	const FGradeJump Dived = RunTimedJump(30.0f, 0.66f, 1.8f, 0.7f);
	TestTrue(TEXT("Both rides came back down"), Undived.bCameDown && Dived.bCameDown);
	if (!Undived.bCameDown || !Dived.bCameDown)
	{
		return false;
	}
	UE_LOG(LogKiteSurf, Log, TEXT("DivedBigAirStomps: undived %.2f m/s, kite %.1f deg, %.2f g, %s; dived %.2f m/s, kite %.1f deg, %.2f g, %s"),
		Undived.LandingSinkMS, Undived.LandingKiteElevationDeg, Undived.LandingG, *GradeName(Undived.Verdict.Grade),
		Dived.LandingSinkMS, Dived.LandingKiteElevationDeg, Dived.LandingG, *GradeName(Dived.Verdict.Grade));

	TestTrue(FString::Printf(TEXT("The dive softens the landing (%.2f g against %.2f g undived)"), Dived.LandingG, Undived.LandingG), Dived.LandingG < Undived.LandingG);
	TestTrue(FString::Printf(TEXT("The kite is still above 45 deg at touchdown (%.1f deg)"), Dived.LandingKiteElevationDeg), Dived.LandingKiteElevationDeg > 45.0f);
	TestTrue(FString::Printf(TEXT("Soft enough to stomp (%.2f g, StompedMaxLandingG 4)"), Dived.LandingG), Dived.LandingG <= 4.0f);
	TestEqual(FString::Printf(TEXT("Graded stomped (%.2f g, kite %.1f deg)"), Dived.LandingG, Dived.LandingKiteElevationDeg), Dived.Verdict.Grade, ELandingGrade::Stomped);
	return true;
}

// Pure evaluator check (batch C, docs/tricks/review.md section 4): the raw sink rate, by itself,
// never grades a landing down any more; only the landing g (which already reflects the absorb
// distance and the crouch) and the kite's elevation do. Complements
// KiteSurf.Trick.HotLandingIsSketchy's sink-alone case with a sweep across both inputs.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickSinkAloneDoesNotGrade, "KiteSurf.Trick.SinkAloneDoesNotGrade", TrickLandingGradeTest::Flags)

bool FKiteSurfTrickSinkAloneDoesNotGrade::RunTest(const FString& Parameters)
{
	using namespace TrickLandingGradeTest;

	// A kite held overhead: whatever the sink, a low g keeps the grade, a high g alone demotes it.
	for (const float SinkMS : { 0.0f, 6.0f, 6.01f, 8.0f, 20.0f, 1000.0f })
	{
		FLandingInputs In;
		In.KiteElevationDeg = 60.0f;
		In.LandingG = 3.0f;
		In.SinkMS = SinkMS;
		const FLandingVerdict V = LandingEvaluator::Evaluate(In);
		TestEqual(FString::Printf(TEXT("Sink %.0f m/s, g 3: still stomped"), SinkMS), V.Grade, ELandingGrade::Stomped);
		TestEqual(FString::Printf(TEXT("Sink %.0f m/s, g 3: no cause"), SinkMS), V.Cause, ELandingCause::None);
	}

	// The same sweep at a landing g just over SketchyMinLandingG: too hard every time, from the g,
	// whether the sink reported alongside it is slow or fast.
	for (const float SinkMS : { 0.0f, 1.0f, 6.0f, 50.0f })
	{
		FLandingInputs In;
		In.KiteElevationDeg = 60.0f;
		In.LandingG = 8.01f;
		In.SinkMS = SinkMS;
		const FLandingVerdict V = LandingEvaluator::Evaluate(In);
		TestEqual(FString::Printf(TEXT("Sink %.0f m/s, g 8.01: sketchy"), SinkMS), V.Grade, ELandingGrade::Sketchy);
		TestEqual(FString::Printf(TEXT("Sink %.0f m/s, g 8.01: too hard"), SinkMS), V.Cause, ELandingCause::TooHard);
	}

	// Two landings that differ only in sink grade identically.
	{
		FLandingInputs Slow;
		Slow.KiteElevationDeg = 60.0f;
		Slow.LandingG = 5.0f;
		Slow.SinkMS = 1.0f;
		FLandingInputs Fast = Slow;
		Fast.SinkMS = 500.0f;
		const FLandingVerdict SlowV = LandingEvaluator::Evaluate(Slow);
		const FLandingVerdict FastV = LandingEvaluator::Evaluate(Fast);
		TestEqual(TEXT("Grade does not depend on the sink"), *GradeName(SlowV.Grade), *GradeName(FastV.Grade));
		TestEqual(TEXT("Cause does not depend on the sink"), *CauseName(SlowV.Cause), *CauseName(FastV.Cause));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
