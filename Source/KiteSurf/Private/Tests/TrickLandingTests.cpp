#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Tricks/LandingEvaluator.h"
#include "Tricks/TrickScoring.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Named, not anonymous, so a unity build cannot merge these with another file's helpers.
namespace TrickLandingTest
{
	/** A landing that is stomped on every count: kite 60 deg, 3 g, upright, nothing else wrong. */
	FLandingInputs Good(float TiltDeg = 0.0f, float YawDeg = 0.0f)
	{
		FLandingInputs In;
		In.TiltDeg = TiltDeg;
		In.YawOffVelocityDeg = YawDeg;
		In.KiteElevationDeg = 60.0f;
		In.LandingG = 3.0f;
		In.SinkMS = 3.0f;
		return In;
	}

	FString GradeName(ELandingGrade Grade)
	{
		return StaticEnum<ELandingGrade>()->GetNameStringByValue(static_cast<int64>(Grade));
	}

	FString CauseName(ELandingCause Cause)
	{
		return StaticEnum<ELandingCause>()->GetNameStringByValue(static_cast<int64>(Cause));
	}

	/** Checks the grade and cause, and that the speed retention is the grade's. */
	void Expect(FAutomationTestBase& Test, const FString& What, const FLandingInputs& In, ELandingGrade Grade,
		ELandingCause Cause = ELandingCause::None, const FLandingThresholds& T = FLandingThresholds())
	{
		const FLandingVerdict V = LandingEvaluator::Evaluate(In, T);
		Test.TestEqual(What + TEXT(": grade (") + GradeName(V.Grade) + TEXT(")"), *GradeName(V.Grade), *GradeName(Grade));
		Test.TestEqual(What + TEXT(": cause (") + CauseName(V.Cause) + TEXT(")"), *CauseName(V.Cause), *CauseName(Cause));
		Test.TestNearlyEqual(What + TEXT(": speed retention"), V.SpeedRetention, T.SpeedRetentionFor(Grade), 1e-6f);
	}

	FQuat AboutDeg(const FVector& Axis, float Deg)
	{
		return FQuat(Axis.GetSafeNormal(), FMath::DegreesToRadians(Deg));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickLandingGrades, "KiteSurf.Trick.LandingGrades",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickLandingGrades::RunTest(const FString& Parameters)
{
	using namespace TrickLandingTest;
	constexpr float Eps = 0.01f;
	const float NaN = std::numeric_limits<float>::quiet_NaN();

	// Tilt table (deg), yaw 0: each limit is inclusive.
	Expect(*this, TEXT("Tilt 0"), Good(0.0f), ELandingGrade::Stomped);
	Expect(*this, TEXT("Tilt 15"), Good(15.0f), ELandingGrade::Stomped);
	Expect(*this, TEXT("Tilt 15.01"), Good(15.0f + Eps), ELandingGrade::Clean);
	Expect(*this, TEXT("Tilt 30"), Good(30.0f), ELandingGrade::Clean);
	Expect(*this, TEXT("Tilt 30.01"), Good(30.0f + Eps), ELandingGrade::Sketchy);
	Expect(*this, TEXT("Tilt 50"), Good(50.0f), ELandingGrade::Sketchy);
	Expect(*this, TEXT("Tilt 50.01"), Good(50.0f + Eps), ELandingGrade::Crash, ELandingCause::UnderRotated);
	Expect(*this, TEXT("Tilt -20 counts as 20"), Good(-20.0f), ELandingGrade::Clean);

	// Yaw table (deg), tilt 0.
	Expect(*this, TEXT("Yaw 20"), Good(0.0f, 20.0f), ELandingGrade::Stomped);
	Expect(*this, TEXT("Yaw 20.01"), Good(0.0f, 20.0f + Eps), ELandingGrade::Clean);
	Expect(*this, TEXT("Yaw 45"), Good(0.0f, 45.0f), ELandingGrade::Clean);
	Expect(*this, TEXT("Yaw 45.01"), Good(0.0f, 45.0f + Eps), ELandingGrade::Sketchy);
	Expect(*this, TEXT("Yaw 75"), Good(0.0f, 75.0f), ELandingGrade::Sketchy);
	Expect(*this, TEXT("Yaw 75.01"), Good(0.0f, 75.0f + Eps), ELandingGrade::Crash, ELandingCause::Sideways);
	Expect(*this, TEXT("Yaw 90 (today's CrashRecovery case)"), Good(0.0f, 90.0f), ELandingGrade::Crash, ELandingCause::Sideways);

	// The worse of tilt and yaw sets the grade.
	Expect(*this, TEXT("Tilt 25, yaw 10"), Good(25.0f, 10.0f), ELandingGrade::Clean);
	Expect(*this, TEXT("Tilt 10, yaw 40"), Good(10.0f, 40.0f), ELandingGrade::Clean);
	Expect(*this, TEXT("Tilt 40, yaw 10"), Good(40.0f, 10.0f), ELandingGrade::Sketchy);
	Expect(*this, TEXT("Tilt 10, yaw 60"), Good(10.0f, 60.0f), ELandingGrade::Sketchy);
	Expect(*this, TEXT("Tilt 30, yaw 45"), Good(30.0f, 45.0f), ELandingGrade::Clean);
	Expect(*this, TEXT("Tilt 50, yaw 75"), Good(50.0f, 75.0f), ELandingGrade::Sketchy);

	// Stomped also needs the kite up and a soft landing; otherwise the same landing is clean.
	{
		FLandingInputs In = Good(10.0f, 10.0f);
		In.KiteElevationDeg = 45.0f;
		Expect(*this, TEXT("Kite at 45 deg"), In, ELandingGrade::Stomped);
		In.KiteElevationDeg = 60.0f;
		In.LandingG = 4.0f;
		Expect(*this, TEXT("4 g"), In, ELandingGrade::Stomped);
		In.LandingG = 4.0f + Eps;
		Expect(*this, TEXT("4.01 g"), In, ELandingGrade::Clean);
		In.LandingG = 5.0f;
		Expect(*this, TEXT("5 g"), In, ELandingGrade::Clean);
		In.LandingG = 8.0f;
		Expect(*this, TEXT("8 g"), In, ELandingGrade::Clean);
		In.LandingG = 8.0f + Eps;
		Expect(*this, TEXT("8.01 g"), In, ELandingGrade::Sketchy, ELandingCause::TooHard);
		In.LandingG = 10.0f;
		Expect(*this, TEXT("10 g"), In, ELandingGrade::Sketchy, ELandingCause::TooHard);
	}

	// Speed retention per grade (estimates): stomped keeps more than clean, sketchy less, a crash none.
	{
		const FLandingThresholds T;
		TestNearlyEqual(TEXT("Stomped keeps 0.85"), LandingEvaluator::Evaluate(Good(), T).SpeedRetention, 0.85f, 1e-6f);
		TestNearlyEqual(TEXT("Clean keeps 0.8"), LandingEvaluator::Evaluate(Good(25.0f), T).SpeedRetention, 0.8f, 1e-6f);
		TestNearlyEqual(TEXT("Sketchy keeps 0.6"), LandingEvaluator::Evaluate(Good(40.0f), T).SpeedRetention, 0.6f, 1e-6f);
		TestNearlyEqual(TEXT("Crash keeps 0"), LandingEvaluator::Evaluate(Good(60.0f), T).SpeedRetention, 0.0f, 1e-6f);
	}

	// Thresholds are honoured, not hard-coded.
	{
		FLandingThresholds Strict;
		Strict.Stomped = FLandingGradeLimits(5.0f, 5.0f);
		Expect(*this, TEXT("Tilt 10 with a 5 deg stomped limit"), Good(10.0f), ELandingGrade::Clean, ELandingCause::None, Strict);
	}

	// A broken input grades down, never up.
	Expect(*this, TEXT("NaN tilt"), Good(NaN), ELandingGrade::Crash, ELandingCause::UnderRotated);
	Expect(*this, TEXT("NaN yaw"), Good(0.0f, NaN), ELandingGrade::Crash, ELandingCause::Sideways);
	{
		FLandingInputs In = Good();
		In.LandingG = NaN;
		Expect(*this, TEXT("NaN landing g"), In, ELandingGrade::Crash, ELandingCause::TooHard);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickSwitchLandingIsClean, "KiteSurf.Trick.SwitchLandingIsClean",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickSwitchLandingIsClean::RunTest(const FString& Parameters)
{
	using namespace TrickLandingTest;

	TestNearlyEqual(TEXT("Fold 5"), LandingEvaluator::FoldYawDeg(5.0f), 5.0f, 1e-4f);
	TestNearlyEqual(TEXT("Fold 175"), LandingEvaluator::FoldYawDeg(175.0f), 5.0f, 1e-4f);
	TestNearlyEqual(TEXT("Fold 185"), LandingEvaluator::FoldYawDeg(185.0f), 5.0f, 1e-4f);
	TestNearlyEqual(TEXT("Fold -5"), LandingEvaluator::FoldYawDeg(-5.0f), 5.0f, 1e-4f);
	TestNearlyEqual(TEXT("Fold 355"), LandingEvaluator::FoldYawDeg(355.0f), 5.0f, 1e-4f);
	TestNearlyEqual(TEXT("Fold 540 (a half turn more)"), LandingEvaluator::FoldYawDeg(540.0f), 0.0f, 1e-4f);
	TestNearlyEqual(TEXT("Fold 90 stays 90"), LandingEvaluator::FoldYawDeg(90.0f), 90.0f, 1e-4f);
	TestNearlyEqual(TEXT("Fold 120"), LandingEvaluator::FoldYawDeg(120.0f), 60.0f, 1e-4f);

	// Riding away tail first is the same landing as nose first.
	const FLandingVerdict Forward = LandingEvaluator::Evaluate(Good(0.0f, 5.0f));
	TestEqual(TEXT("Yaw 5 is stomped"), *GradeName(Forward.Grade), *GradeName(ELandingGrade::Stomped));
	for (const float Yaw : { 175.0f, 185.0f, -175.0f, -185.0f, 355.0f })
	{
		const FLandingVerdict Switch = LandingEvaluator::Evaluate(Good(0.0f, Yaw));
		const FString What = FString::Printf(TEXT("Yaw %.0f deg"), Yaw);
		TestEqual(What + TEXT(": same grade as 5"), *GradeName(Switch.Grade), *GradeName(Forward.Grade));
		TestEqual(What + TEXT(": same cause as 5"), *CauseName(Switch.Cause), *CauseName(Forward.Cause));
		TestNearlyEqual(What + TEXT(": same speed retention as 5"), Switch.SpeedRetention, Forward.SpeedRetention, 1e-6f);
	}

	// Switch with a little more yaw is clean, as forwards; across the board is still a crash.
	Expect(*this, TEXT("Yaw 155 (25 off the tail)"), Good(0.0f, 155.0f), ELandingGrade::Clean);
	Expect(*this, TEXT("Yaw 205 (25 off the tail)"), Good(0.0f, 205.0f), ELandingGrade::Clean);
	Expect(*this, TEXT("Yaw 100 (80 off the tail)"), Good(0.0f, 100.0f), ELandingGrade::Crash, ELandingCause::Sideways);

	// The record-only shortcut agrees.
	TestEqual(TEXT("GradeLanding at yaw 175"), *GradeName(TrickScoring::GradeLanding(175.0f, 3.0f, 60.0f, false)),
		*GradeName(ELandingGrade::Stomped));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickUnderRotatedRollCrashes, "KiteSurf.Trick.UnderRotatedRollCrashes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickUnderRotatedRollCrashes::RunTest(const FString& Parameters)
{
	using namespace TrickLandingTest;

	// From the sign of ErrorAlongSpin directly.
	{
		FLandingInputs In = Good(55.0f, 10.0f);
		In.ErrorAlongSpin = 0.5f;
		Expect(*this, TEXT("Tilt 55, error along the spin +0.5"), In, ELandingGrade::Crash, ELandingCause::UnderRotated);
		In.ErrorAlongSpin = -0.5f;
		Expect(*this, TEXT("Tilt 55, error along the spin -0.5"), In, ELandingGrade::Crash, ELandingCause::OverRotated);
		In.ErrorAlongSpin = 0.0f;
		Expect(*this, TEXT("Tilt 55 without a spin"), In, ELandingGrade::Crash, ELandingCause::UnderRotated);
		In.TiltDeg = 120.0f;
		In.ErrorAlongSpin = -0.2f;
		Expect(*this, TEXT("Tilt 120, past the landing"), In, ELandingGrade::Crash, ELandingCause::OverRotated);
	}

	// From a roll about +X through the geometry helper: the board turns R degrees from upright.
	const FVector Spin(FMath::DegreesToRadians(180.0f), 0.0f, 0.0f); // rad/s about world +X
	const FVector Velocity(800.0f, 0.0f, -400.0f);                    // cm/s
	auto RollVerdict = [&](float RollDeg)
	{
		const FQuat Board = AboutDeg(FVector::ForwardVector, RollDeg);
		FLandingInputs In = Good();
		LandingEvaluator::ApplyGeometry(
			LandingEvaluator::ComputeGeometry(Board, FVector::UpVector, Velocity, FQuat::Identity, Spin), In);
		return LandingEvaluator::Evaluate(In);
	};
	struct FRollCase { float RollDeg; ELandingGrade Grade; ELandingCause Cause; };
	const FRollCase Cases[] = {
		{ 300.0f, ELandingGrade::Crash, ELandingCause::UnderRotated }, // 60 deg short
		{ 305.0f, ELandingGrade::Crash, ELandingCause::UnderRotated }, // 55 short
		{ 315.0f, ELandingGrade::Sketchy, ELandingCause::None },       // 45 short
		{ 350.0f, ELandingGrade::Stomped, ELandingCause::None },       // 10 short
		{ 370.0f, ELandingGrade::Stomped, ELandingCause::None },       // 10 past
		{ 415.0f, ELandingGrade::Crash, ELandingCause::OverRotated },  // 55 past
		{ 420.0f, ELandingGrade::Crash, ELandingCause::OverRotated },  // 60 past
	};
	for (const FRollCase& Case : Cases)
	{
		const FLandingVerdict V = RollVerdict(Case.RollDeg);
		const FString What = FString::Printf(TEXT("Roll of %.0f deg"), Case.RollDeg);
		TestEqual(What + TEXT(": grade"), *GradeName(V.Grade), *GradeName(Case.Grade));
		TestEqual(What + TEXT(": cause"), *CauseName(V.Cause), *CauseName(Case.Cause));
	}

	// The same rolls the other way round: the sign follows the spin, not the side.
	const FQuat Back = AboutDeg(FVector::ForwardVector, -300.0f);
	FLandingInputs In = Good();
	LandingEvaluator::ApplyGeometry(LandingEvaluator::ComputeGeometry(Back, FVector::UpVector, Velocity, FQuat::Identity, -Spin), In);
	Expect(*this, TEXT("Roll of -300 deg about -X"), In, ELandingGrade::Crash, ELandingCause::UnderRotated);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickHotLandingIsSketchy, "KiteSurf.Trick.HotLandingIsSketchy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickHotLandingIsSketchy::RunTest(const FString& Parameters)
{
	using namespace TrickLandingTest;

	FLandingInputs In = Good(5.0f, 5.0f);
	Expect(*this, TEXT("Perfect landing, kite at 60 deg"), In, ELandingGrade::Stomped);
	In.KiteElevationDeg = 45.0f;
	Expect(*this, TEXT("Kite at 45 deg"), In, ELandingGrade::Stomped);
	In.KiteElevationDeg = 44.99f;
	Expect(*this, TEXT("Kite at 44.99 deg"), In, ELandingGrade::Sketchy, ELandingCause::KiteTooLow);
	In.KiteElevationDeg = 30.0f;
	Expect(*this, TEXT("Kite at 30 deg"), In, ELandingGrade::Sketchy, ELandingCause::KiteTooLow);
	In.KiteElevationDeg = -5.0f;
	Expect(*this, TEXT("Kite in the water"), In, ELandingGrade::Sketchy, ELandingCause::KiteTooLow);

	// Batch C (docs/tricks/review.md section 4): a raw sink rate, however fast, no longer grades a
	// landing down on its own; a big air's long, crouched absorb already turns the sink into the
	// landing g, which is what too hard reads instead (KiteSurf.Trick.SinkAloneDoesNotGrade).
	In = Good(5.0f, 5.0f);
	In.SinkMS = 6.0f;
	Expect(*this, TEXT("Sink 6 m/s"), In, ELandingGrade::Stomped);
	In.SinkMS = 60.0f;
	Expect(*this, TEXT("Sink 60 m/s, g still 3"), In, ELandingGrade::Stomped);

	// The board's own hot flag.
	In = Good(5.0f, 5.0f);
	In.bHotLanding = true;
	Expect(*this, TEXT("Board's hot flag"), In, ELandingGrade::Sketchy, ELandingCause::KiteTooLow);

	// The kite is named when both apply.
	In = Good(5.0f, 5.0f);
	In.KiteElevationDeg = 30.0f;
	In.LandingG = 9.0f;
	Expect(*this, TEXT("Kite low and landing hard"), In, ELandingGrade::Sketchy, ELandingCause::KiteTooLow);

	// A hot landing that is already sketchy stays sketchy, and a hot crash stays a crash.
	In = Good(40.0f, 5.0f);
	In.KiteElevationDeg = 30.0f;
	Expect(*this, TEXT("Kite low, tilt 40"), In, ELandingGrade::Sketchy, ELandingCause::KiteTooLow);
	In.TiltDeg = 55.0f;
	Expect(*this, TEXT("Kite low, tilt 55"), In, ELandingGrade::Crash, ELandingCause::UnderRotated);
	In = Good(5.0f, 80.0f);
	In.KiteElevationDeg = 30.0f;
	Expect(*this, TEXT("Kite low, yaw 80"), In, ELandingGrade::Crash, ELandingCause::Sideways);

	// The record-only shortcut agrees on the kite.
	TestEqual(TEXT("GradeLanding with the kite at 30 deg"), *GradeName(TrickScoring::GradeLanding(5.0f, 3.0f, 30.0f, false)),
		*GradeName(ELandingGrade::Sketchy));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickCrashCauses, "KiteSurf.Trick.CrashCauses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickCrashCauses::RunTest(const FString& Parameters)
{
	using namespace TrickLandingTest;

	FLandingInputs In = Good();
	In.bBoardAttached = false;
	Expect(*this, TEXT("Board off"), In, ELandingGrade::Crash, ELandingCause::BoardOff);

	In = Good();
	In.bBarInHands = false;
	Expect(*this, TEXT("Bar lost"), In, ELandingGrade::Crash, ELandingCause::BarLost);

	In = Good();
	In.bPassInProgress = true;
	Expect(*this, TEXT("Pass unfinished"), In, ELandingGrade::Crash, ELandingCause::PassUnfinished);

	In = Good();
	In.BodyUpDot = -0.2f;
	Expect(*this, TEXT("Body up dot -0.2"), In, ELandingGrade::Crash, ELandingCause::Inverted);
	In.BodyUpDot = -0.01f;
	Expect(*this, TEXT("Body up dot -0.01"), In, ELandingGrade::Crash, ELandingCause::Inverted);
	In.BodyUpDot = 0.0f;
	Expect(*this, TEXT("Body up dot 0 (lying flat) is not inverted"), In, ELandingGrade::Stomped);

	In = Good();
	In.LandingG = 10.0f + 0.01f;
	Expect(*this, TEXT("10.01 g"), In, ELandingGrade::Crash, ELandingCause::TooHard);
	{
		FLandingThresholds Phase2;
		Phase2.CrashLandingG = 8.0f;
		In.LandingG = 8.5f;
		Expect(*this, TEXT("8.5 g with phase 2's crash g of 8"), In, ELandingGrade::Crash, ELandingCause::TooHard, Phase2);
	}

	// Every crash keeps no speed, and a perfect board does not save a crash.
	In = Good();
	In.bBoardAttached = false;
	TestNearlyEqual(TEXT("Board off keeps no speed"), LandingEvaluator::Evaluate(In).SpeedRetention, 0.0f, 1e-6f);

	// The rider's state is named before the board's attitude, in the documented order.
	In = Good(60.0f, 80.0f);
	In.bBoardAttached = false;
	In.bBarInHands = false;
	In.bPassInProgress = true;
	In.BodyUpDot = -1.0f;
	In.LandingG = 20.0f;
	Expect(*this, TEXT("Everything wrong"), In, ELandingGrade::Crash, ELandingCause::BoardOff);
	In.bBoardAttached = true;
	Expect(*this, TEXT("All but the board"), In, ELandingGrade::Crash, ELandingCause::BarLost);
	In.bBarInHands = true;
	Expect(*this, TEXT("Pass, inverted, hard, tilted, sideways"), In, ELandingGrade::Crash, ELandingCause::PassUnfinished);
	In.bPassInProgress = false;
	Expect(*this, TEXT("Inverted, hard, tilted, sideways"), In, ELandingGrade::Crash, ELandingCause::Inverted);
	In.BodyUpDot = 1.0f;
	Expect(*this, TEXT("Hard, tilted, sideways"), In, ELandingGrade::Crash, ELandingCause::TooHard);
	In.LandingG = 3.0f;
	Expect(*this, TEXT("Tilted, sideways"), In, ELandingGrade::Crash, ELandingCause::UnderRotated);
	In.TiltDeg = 10.0f;
	Expect(*this, TEXT("Sideways"), In, ELandingGrade::Crash, ELandingCause::Sideways);

	// The record-only shortcut: the board decides crashes.
	TestEqual(TEXT("GradeLanding: crashed"), *GradeName(TrickScoring::GradeLanding(5.0f, 3.0f, 60.0f, true)),
		*GradeName(ELandingGrade::Crash));
	TestEqual(TEXT("GradeLanding: 20 g but the board rode away"), *GradeName(TrickScoring::GradeLanding(5.0f, 20.0f, 60.0f, false)),
		*GradeName(ELandingGrade::Sketchy));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickLandingGeometry, "KiteSurf.Trick.LandingGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickLandingGeometry::RunTest(const FString& Parameters)
{
	using namespace TrickLandingTest;
	constexpr float TolDeg = 0.01f;
	const FVector Up = FVector::UpVector;
	const FVector Travel(800.0f, 0.0f, -300.0f); // cm/s: along +X and sinking

	auto Geo = [](const FQuat& Board, const FVector& Normal, const FVector& Velocity, const FQuat& Body = FQuat::Identity,
		const FVector& Spin = FVector::ZeroVector)
	{
		return LandingEvaluator::ComputeGeometry(Board, Normal, Velocity, Body, Spin);
	};

	{
		const FLandingGeometry G = Geo(FQuat::Identity, Up, Travel);
		TestNearlyEqual(TEXT("Flat board: tilt (deg)"), G.TiltDeg, 0.0f, TolDeg);
		TestNearlyEqual(TEXT("Flat board along travel: yaw (deg); the sink does not count"), G.YawOffVelocityDeg, 0.0f, TolDeg);
		TestNearlyEqual(TEXT("Upright body: up dot"), G.BodyUpDot, 1.0f, 1e-5f);
		TestNearlyEqual(TEXT("No spin: error along spin"), G.ErrorAlongSpin, 0.0f, 1e-6f);
	}

	// Yaw about the normal: either end counts.
	TestNearlyEqual(TEXT("Yawed 30: yaw (deg)"), Geo(AboutDeg(Up, 30.0f), Up, Travel).YawOffVelocityDeg, 30.0f, TolDeg);
	TestNearlyEqual(TEXT("Yawed -30: yaw (deg)"), Geo(AboutDeg(Up, -30.0f), Up, Travel).YawOffVelocityDeg, 30.0f, TolDeg);
	TestNearlyEqual(TEXT("Yawed 120: yaw (deg)"), Geo(AboutDeg(Up, 120.0f), Up, Travel).YawOffVelocityDeg, 60.0f, TolDeg);
	TestNearlyEqual(TEXT("Yawed 180 (switch): yaw (deg)"), Geo(AboutDeg(Up, 180.0f), Up, Travel).YawOffVelocityDeg, 0.0f, TolDeg);
	TestNearlyEqual(TEXT("Yawed 90: yaw (deg)"), Geo(AboutDeg(Up, 90.0f), Up, Travel).YawOffVelocityDeg, 90.0f, TolDeg);
	TestNearlyEqual(TEXT("Yawed 30: tilt stays 0 (deg)"), Geo(AboutDeg(Up, 30.0f), Up, Travel).TiltDeg, 0.0f, TolDeg);

	// Pitch and roll are tilt.
	TestNearlyEqual(TEXT("Pitched 20: tilt (deg)"), Geo(AboutDeg(FVector::RightVector, 20.0f), Up, Travel).TiltDeg, 20.0f, TolDeg);
	TestNearlyEqual(TEXT("Pitched 20: yaw (deg)"), Geo(AboutDeg(FVector::RightVector, 20.0f), Up, Travel).YawOffVelocityDeg, 0.0f, TolDeg);
	TestNearlyEqual(TEXT("Rolled 35: tilt (deg)"), Geo(AboutDeg(FVector::ForwardVector, 35.0f), Up, Travel).TiltDeg, 35.0f, TolDeg);
	TestNearlyEqual(TEXT("Upside down: tilt (deg)"), Geo(AboutDeg(FVector::ForwardVector, 180.0f), Up, Travel).TiltDeg, 180.0f, TolDeg);

	// Yaw 30 then a local pitch of 10: both read back separately.
	{
		const FQuat Board = AboutDeg(Up, 30.0f) * AboutDeg(FVector::RightVector, 10.0f);
		const FLandingGeometry G = Geo(Board, Up, Travel);
		TestNearlyEqual(TEXT("Yaw 30 + pitch 10: tilt (deg)"), G.TiltDeg, 10.0f, TolDeg);
		TestNearlyEqual(TEXT("Yaw 30 + pitch 10: yaw (deg)"), G.YawOffVelocityDeg, 30.0f, TolDeg);
	}

	// On a swell face: tilt is from the local normal, not world up.
	{
		const FVector Slope = AboutDeg(FVector::RightVector, 12.0f).RotateVector(Up);
		const FQuat Board = AboutDeg(FVector::RightVector, 12.0f);
		TestNearlyEqual(TEXT("Board following a 12 deg slope: tilt (deg)"), Geo(Board, Slope * 3.0f, Travel).TiltDeg, 0.0f, TolDeg);
		TestNearlyEqual(TEXT("Flat board on a 12 deg slope: tilt (deg)"), Geo(FQuat::Identity, Slope, Travel).TiltDeg, 12.0f, TolDeg);
		TestNearlyEqual(TEXT("Zero normal counts as world up: tilt (deg)"),
			Geo(AboutDeg(FVector::RightVector, 12.0f), FVector::ZeroVector, Travel).TiltDeg, 12.0f, TolDeg);
	}

	// No travel along the water: yaw 0.
	TestNearlyEqual(TEXT("Straight down: yaw (deg)"), Geo(AboutDeg(Up, 60.0f), Up, FVector(0, 0, -500)).YawOffVelocityDeg, 0.0f, TolDeg);
	TestNearlyEqual(TEXT("At rest: yaw (deg)"), Geo(AboutDeg(Up, 60.0f), Up, FVector::ZeroVector).YawOffVelocityDeg, 0.0f, TolDeg);

	// The rider's up against world up.
	TestNearlyEqual(TEXT("Body rolled 60: up dot"), Geo(FQuat::Identity, Up, Travel, AboutDeg(FVector::ForwardVector, 60.0f)).BodyUpDot, 0.5f, 1e-5f);
	TestNearlyEqual(TEXT("Body on its side: up dot"), Geo(FQuat::Identity, Up, Travel, AboutDeg(FVector::ForwardVector, 90.0f)).BodyUpDot, 0.0f, 1e-5f);
	TestNearlyEqual(TEXT("Body upside down: up dot"), Geo(FQuat::Identity, Up, Travel, AboutDeg(FVector::RightVector, 180.0f)).BodyUpDot, -1.0f, 1e-5f);
	TestNearlyEqual(TEXT("Body up dot ignores the slope"),
		Geo(FQuat::Identity, AboutDeg(FVector::RightVector, 12.0f).RotateVector(Up), Travel).BodyUpDot, 1.0f, 1e-5f);

	// Error along the spin: a roll about +X stopped 60 deg short has the error axis along the spin
	// (+1), 60 deg past against it (-1).
	{
		const FVector Spin(3.0f, 0.0f, 0.0f);
		TestNearlyEqual(TEXT("60 short: error along spin"), Geo(AboutDeg(FVector::ForwardVector, -60.0f), Up, Travel, FQuat::Identity, Spin).ErrorAlongSpin, 1.0f, 1e-4f);
		TestNearlyEqual(TEXT("60 past: error along spin"), Geo(AboutDeg(FVector::ForwardVector, 60.0f), Up, Travel, FQuat::Identity, Spin).ErrorAlongSpin, -1.0f, 1e-4f);
		TestNearlyEqual(TEXT("10 short: error along spin"), Geo(AboutDeg(FVector::ForwardVector, -10.0f), Up, Travel, FQuat::Identity, Spin).ErrorAlongSpin, 1.0f, 1e-4f);
		TestNearlyEqual(TEXT("Spin across the error: nothing along it"), Geo(AboutDeg(FVector::ForwardVector, 60.0f), Up, Travel, FQuat::Identity, FVector(0, 0, 3)).ErrorAlongSpin, 0.0f, 1e-4f);
		TestNearlyEqual(TEXT("Spin at 60 deg to the error axis: cos 60"),
			Geo(AboutDeg(FVector::ForwardVector, -60.0f), Up, Travel, FQuat::Identity, FVector(1.0f, FMath::Sqrt(3.0f), 0.0f)).ErrorAlongSpin, 0.5f, 1e-4f);
		TestNearlyEqual(TEXT("The spin's size does not matter"), Geo(AboutDeg(FVector::ForwardVector, -60.0f), Up, Travel, FQuat::Identity, Spin * 100.0f).ErrorAlongSpin, 1.0f, 1e-4f);
		TestNearlyEqual(TEXT("Flat board: no error"), Geo(FQuat::Identity, Up, Travel, FQuat::Identity, Spin).ErrorAlongSpin, 0.0f, 1e-6f);
	}

	// ApplyGeometry copies the four fields and nothing else.
	{
		FLandingInputs In = Good();
		In.KiteElevationDeg = 33.0f;
		FLandingGeometry G;
		G.TiltDeg = 12.0f;
		G.YawOffVelocityDeg = 34.0f;
		G.BodyUpDot = 0.5f;
		G.ErrorAlongSpin = -0.25f;
		LandingEvaluator::ApplyGeometry(G, In);
		TestEqual(TEXT("Applied tilt"), In.TiltDeg, 12.0f);
		TestEqual(TEXT("Applied yaw"), In.YawOffVelocityDeg, 34.0f);
		TestEqual(TEXT("Applied body up dot"), In.BodyUpDot, 0.5f);
		TestEqual(TEXT("Applied error along spin"), In.ErrorAlongSpin, -0.25f);
		TestEqual(TEXT("Kite elevation untouched"), In.KiteElevationDeg, 33.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickGradeLandingAgreesWithEvaluator, "KiteSurf.Trick.GradeLandingAgreesWithEvaluator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickGradeLandingAgreesWithEvaluator::RunTest(const FString& Parameters)
{
	using namespace TrickLandingTest;

	// GradeLanding is Evaluate with tilt 0 and the board's crash decision: over a grid of the
	// record's facts, a landing the board rode away from gets Evaluate's grade, a crash there
	// reading as sketchy.
	int32 Checked = 0;
	for (const float Yaw : { 0.0f, 20.0f, 20.1f, 44.0f, 46.0f, 74.0f, 80.0f, 160.0f, 178.0f })
	{
		for (const float G : { 1.0f, 4.0f, 4.1f, 8.0f, 8.1f, 10.0f, 12.0f })
		{
			for (const float Kite : { 10.0f, 44.9f, 45.0f, 80.0f })
			{
				FLandingInputs In;
				In.YawOffVelocityDeg = Yaw;
				In.LandingG = G;
				In.KiteElevationDeg = Kite;
				ELandingGrade Expected = LandingEvaluator::Evaluate(In).Grade;
				Expected = Expected == ELandingGrade::Crash ? ELandingGrade::Sketchy : Expected;
				const ELandingGrade Shortcut = TrickScoring::GradeLanding(Yaw, G, Kite, false);
				if (Shortcut != Expected)
				{
					AddError(FString::Printf(TEXT("Yaw %.1f, %.1f g, kite %.1f: GradeLanding %s, Evaluate %s"),
						Yaw, G, Kite, *GradeName(Shortcut), *GradeName(Expected)));
				}
				TestEqual(FString::Printf(TEXT("Yaw %.1f, %.1f g, kite %.1f, crashed"), Yaw, G, Kite),
					GradeName(TrickScoring::GradeLanding(Yaw, G, Kite, true)), *GradeName(ELandingGrade::Crash));
				++Checked;
			}
		}
	}
	TestEqual(TEXT("Grid size"), Checked, 9 * 7 * 4);

	// Custom FLandingGradeSettings reach the evaluator.
	FLandingGradeSettings Loose;
	Loose.StompedMaxYawDeg = 40.0f;
	Loose.HotKiteElevationDeg = 20.0f;
	TestEqual(TEXT("Yaw 35, kite 30 with loose settings"), *GradeName(TrickScoring::GradeLanding(35.0f, 3.0f, 30.0f, false, Loose)),
		*GradeName(ELandingGrade::Stomped));
	return true;
}

#endif
