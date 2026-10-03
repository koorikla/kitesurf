#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Templates/Function.h"
#include "Tricks/BarState.h"
#include "Tricks/TrickNaming.h"
#include "Tricks/TrickSignature.h"

#if WITH_DEV_AUTOMATION_TESTS

// Named, not anonymous, so a unity build cannot merge these with another file's helpers.
namespace TrickBarTest
{
	constexpr float BodyWeightN = 834.0f;
	constexpr float Dt60 = 1.0f / 60.0f;
	constexpr float Dt240 = 1.0f / 240.0f;
	/** Moves scripted event times off the 1/240 s step grid, so no event sits on a step boundary. */
	constexpr float OffGrid = 0.002f;

	/** The kite downwind along +X at 45 deg elevation. */
	FVector KiteDir()
	{
		return FVector(1.0f, 0.0f, 1.0f).GetSafeNormal();
	}

	/**
	 * The body turned BackDeg backside from facing the kite (Front +X, Up +Z, Right +Y). Backside
	 * turns the chest to the tail first; the nose is along NoseSide x Right, so the tail is on the
	 * other side and backside is a turn about Up by -NoseSide x angle.
	 */
	FQuat Turned(float BacksideDeg, float NoseSide = 1.0f)
	{
		return FQuat(FVector::UpVector, FMath::DegreesToRadians(-NoseSide * BacksideDeg));
	}

	FBarInputs Inputs(float TensionBW, const FQuat& Body, bool bAirborne, float NoseSide = 1.0f)
	{
		FBarInputs In;
		In.TensionN = TensionBW * BodyWeightN;
		In.BodyWeightN = BodyWeightN;
		In.LineDirWorld = KiteDir();
		In.Body = Body;
		In.NoseSideSign = NoseSide;
		In.bAirborne = bAirborne;
		In.bOnWaterRideable = !bAirborne;
		return In;
	}

	/**
	 * Unhooked, as if already stepped facing the kite: the line azimuth is seeded at 0, so a
	 * script that starts turning on its first step loses none of the turn to the seed.
	 */
	FBarState Unhooked()
	{
		FBarState S;
		S.bHooked = false;
		S.bHasLineAngle = true;
		S.LastLineAngleDeg = 0.0f;
		return S;
	}

	/** True on the step [T0, T0 + Dt) that holds the instant At. */
	bool During(float At, float T0, float Dt)
	{
		return At >= T0 && At < T0 + Dt;
	}

	/** What a scripted run did, with the time of each first event (s, step end; -1 if none). */
	struct FRun
	{
		FBarState State;
		float LostAt = -1.0f;
		EBarLossCause Cause = EBarLossCause::None;
		float PassStartedAt = -1.0f;
		float PassDoneAt = -1.0f;
		int32 PassesStarted = 0;
		int32 PassesDone = 0;
		ETrickPassKind LastPassKind = ETrickPassKind::None;
		bool bHookedEvent = false;
		bool bUnhookedEvent = false;
		/** CanLandRideable on the last airborne step. */
		bool bRideableAtTouchdown = true;
	};

	/** Steps the machine for Seconds; Script gets the step's start time and Dt and samples at the step's middle. */
	FRun Run(FBarState S, float Dt, float Seconds, TFunctionRef<FBarInputs(float T0, float Dt)> Script,
		const FBarTunables& T = FBarTunables())
	{
		FRun R;
		const int32 Steps = FMath::RoundToInt(Seconds / Dt);
		for (int32 K = 0; K < Steps; ++K)
		{
			const float T0 = K * Dt;
			const FBarInputs In = Script(T0, Dt);
			if (In.bAirborne)
			{
				R.bRideableAtTouchdown = BarStateMachine::CanLandRideable(S, T);
			}
			const FBarEvents E = BarStateMachine::Step(S, In, T, Dt);
			const float TEnd = T0 + Dt;
			if (E.bPassStarted)
			{
				++R.PassesStarted;
				if (R.PassStartedAt < 0.0f)
				{
					R.PassStartedAt = TEnd;
				}
			}
			if (E.bPassDone)
			{
				++R.PassesDone;
				R.LastPassKind = E.PassKind;
				if (R.PassDoneAt < 0.0f)
				{
					R.PassDoneAt = TEnd;
				}
			}
			if (E.Lost != EBarLossCause::None && R.LostAt < 0.0f)
			{
				R.LostAt = TEnd;
				R.Cause = E.Lost;
			}
			R.bHookedEvent |= E.bHooked;
			R.bUnhookedEvent |= E.bUnhooked;
		}
		R.State = S;
		return R;
	}

	FString CauseName(EBarLossCause Cause)
	{
		return StaticEnum<EBarLossCause>()->GetNameStringByValue(static_cast<int64>(Cause));
	}

	FString PlaceName(EBarPlace Place)
	{
		return StaticEnum<EBarPlace>()->GetNameStringByValue(static_cast<int64>(Place));
	}

	FString StanceName(ETrickStance Stance)
	{
		return StaticEnum<ETrickStance>()->GetNameStringByValue(static_cast<int64>(Stance));
	}

	FString KindName(ETrickPassKind Kind)
	{
		return StaticEnum<ETrickPassKind>()->GetNameStringByValue(static_cast<int64>(Kind));
	}

	FString RateName(float Dt)
	{
		return FString::Printf(TEXT("[%d Hz]"), FMath::RoundToInt(1.0f / Dt));
	}

	/** The same outcome at both rates: event, cause, pass counts, and event times within one 1/60 s step. */
	void ExpectSameAtBothRates(FAutomationTestBase& Test, const FString& What, const FRun& A, const FRun& B)
	{
		Test.TestEqual(What + TEXT(": same loss cause at 60 and 240 Hz"), *CauseName(A.Cause), *CauseName(B.Cause));
		Test.TestEqual(What + TEXT(": same passes started at 60 and 240 Hz"), A.PassesStarted, B.PassesStarted);
		Test.TestEqual(What + TEXT(": same passes done at 60 and 240 Hz"), A.PassesDone, B.PassesDone);
		Test.TestEqual(What + TEXT(": same place at 60 and 240 Hz"), *PlaceName(A.State.Place), *PlaceName(B.State.Place));
		if (A.LostAt >= 0.0f && B.LostAt >= 0.0f)
		{
			Test.TestNearlyEqual(What + TEXT(": loss time agrees within a 60 Hz step (s)"), A.LostAt, B.LostAt, Dt60 + 1e-4f);
		}
		if (A.PassDoneAt >= 0.0f && B.PassDoneAt >= 0.0f)
		{
			Test.TestNearlyEqual(What + TEXT(": pass finish time agrees within a 60 Hz step (s)"), A.PassDoneAt, B.PassDoneAt, Dt60 + 1e-4f);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickGripLimitTiming, "KiteSurf.Trick.GripLimitTiming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickGripLimitTiming::RunTest(const FString& Parameters)
{
	using namespace TrickBarTest;
	const FBarTunables T;

	// 1.35 BW for OverSeconds, then 1.2 BW, facing the kite on the water.
	const auto Burst = [](float OverSeconds)
	{
		return [OverSeconds](float T0, float Dt)
		{
			const float Mid = T0 + 0.5f * Dt;
			return Inputs(Mid < OverSeconds ? 1.35f : 1.2f, FQuat::Identity, false);
		};
	};

	FRun Short[2];
	FRun Long[2];
	const float Rates[2] = { Dt60, Dt240 };
	for (int32 I = 0; I < 2; ++I)
	{
		const float Dt = Rates[I];
		const FString Rate = RateName(Dt);

		Short[I] = Run(Unhooked(), Dt, 1.0f, Burst(0.14f));
		TestEqual(Rate + TEXT(" 1.35 BW for 0.14 s then 1.2 BW keeps the bar"), *CauseName(Short[I].Cause), *CauseName(EBarLossCause::None));
		TestTrue(Rate + TEXT(" ...still in front with both hands"), Short[I].State.Place == EBarPlace::Front && Short[I].State.Hands == EBarHands::Both);

		Long[I] = Run(Unhooked(), Dt, 1.0f, Burst(0.16f));
		TestEqual(Rate + TEXT(" 1.35 BW for 0.16 s loses the bar"), *CauseName(Long[I].Cause), *CauseName(EBarLossCause::OverGrip));
		TestEqual(Rate + TEXT(" ...and it stays lost"), *PlaceName(Long[I].State.Place), *PlaceName(EBarPlace::Lost));
		TestTrue(FString::Printf(TEXT("%s ...after the 0.15 s limit and within a step of it (lost at %.4f s)"), *Rate, Long[I].LostAt),
			Long[I].LostAt > T.GripLimitSeconds && Long[I].LostAt <= T.GripLimitSeconds + Dt + Dt60);
		TestEqual(Rate + TEXT(" ...landing cause BarLost"), BarStateMachine::LandingCauseOf(Long[I].Cause), ELandingCause::BarLost);
		TestFalse(Rate + TEXT(" A lost bar cannot be landed"), BarStateMachine::CanLandRideable(Long[I].State));

		// Under the limit holds for ever; short bursts do not add up.
		const FRun Under = Run(Unhooked(), Dt, 2.0f, [](float, float) { return Inputs(1.29f, FQuat::Identity, false); });
		TestEqual(Rate + TEXT(" 1.29 BW for 2 s keeps the bar"), *CauseName(Under.Cause), *CauseName(EBarLossCause::None));
		const FRun Bursts = Run(Unhooked(), Dt, 1.0f, [](float T0, float Step)
		{
			const float Mid = T0 + 0.5f * Step;
			const bool bOver = Mid < 0.1f || (Mid >= 0.15f && Mid < 0.25f);
			return Inputs(bOver ? 1.35f : 1.2f, FQuat::Identity, false);
		});
		TestEqual(Rate + TEXT(" Two 0.1 s bursts with a gap keep the bar"), *CauseName(Bursts.Cause), *CauseName(EBarLossCause::None));

		// Hooked in, the harness takes it: no grip limit.
		const FRun Hooked = Run(FBarState(), Dt, 1.0f, [](float, float) { return Inputs(3.0f, FQuat::Identity, false); });
		TestEqual(Rate + TEXT(" Hooked at 3 BW keeps the bar"), *CauseName(Hooked.Cause), *CauseName(EBarLossCause::None));
		TestTrue(Rate + TEXT(" ...and stays hooked"), Hooked.State.bHooked);
	}
	ExpectSameAtBothRates(*this, TEXT("0.14 s burst"), Short[0], Short[1]);
	ExpectSameAtBothRates(*this, TEXT("0.16 s burst"), Long[0], Long[1]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickPassNeedsSlack, "KiteSurf.Trick.PassNeedsSlack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickPassNeedsSlack::RunTest(const FString& Parameters)
{
	using namespace TrickBarTest;

	TestNearlyEqual(TEXT("Facing the kite, the back is 180 deg from it"), BarStateMachine::BackToKiteDeg(Turned(0.0f), KiteDir()), 180.0f, 0.01f);
	TestNearlyEqual(TEXT("Turned 180, the back is square to the kite"), BarStateMachine::BackToKiteDeg(Turned(180.0f), KiteDir()), 0.0f, 0.01f);
	TestNearlyEqual(TEXT("Turned 60, the back is 120 deg off"), BarStateMachine::BackToKiteDeg(Turned(60.0f), KiteDir()), 120.0f, 0.01f);

	/**
	 * In the air from the start: turn backside to TurnDeg over 0.5 s at SetupBW, hold it, press
	 * pass at 0.6 s; the tension is TensionAt(mid time).
	 */
	const auto Scenario = [](float TurnDeg, TFunction<float(float)> TensionAt)
	{
		return [TurnDeg, TensionAt](float T0, float Dt)
		{
			const float Mid = T0 + 0.5f * Dt;
			FBarInputs In = Inputs(TensionAt(Mid), Turned(TurnDeg * FMath::Min(Mid / 0.5f, 1.0f)), true);
			In.bPassPressed = During(0.6f + OffGrid, T0, Dt);
			return In;
		};
	};
	const auto Constant = [](float BW) { return [BW](float) { return BW; }; };

	FRun Done[2], Loaded[2], Turned120[2], UnderLoad[2], Buffered[2], Late[2];
	const float Rates[2] = { Dt60, Dt240 };
	for (int32 I = 0; I < 2; ++I)
	{
		const float Dt = Rates[I];
		const FString Rate = RateName(Dt);

		// 0.2 BW, back square to the kite: the pass runs 0.25 s and completes.
		Done[I] = Run(Unhooked(), Dt, 1.2f, Scenario(180.0f, Constant(0.2f)));
		TestEqual(Rate + TEXT(" 0.2 BW, back to the kite: one pass started"), Done[I].PassesStarted, 1);
		TestEqual(Rate + TEXT(" ...and done"), Done[I].PassesDone, 1);
		TestEqual(Rate + TEXT(" ...one pass this jump"), Done[I].State.JumpPasses.Num(), 1);
		TestEqual(Rate + TEXT(" ...in the air"), *KindName(Done[I].LastPassKind), *KindName(ETrickPassKind::Air));
		TestNearlyEqual(Rate + TEXT(" ...0.25 s after the press (s)"), Done[I].PassDoneAt - Done[I].PassStartedAt, 0.25f - Dt, Dt + 1e-4f);
		TestEqual(Rate + TEXT(" ...bar behind the back"), *PlaceName(Done[I].State.Place), *PlaceName(EBarPlace::BehindBack));
		TestEqual(Rate + TEXT(" ...in both hands"), Done[I].State.Hands, EBarHands::Both);
		TestNearlyEqual(Rate + TEXT(" ...the wrap switched from +180 to -180 (deg)"), Done[I].State.WrapDeg, -180.0f, 0.5f);
		TestEqual(Rate + TEXT(" No loss"), *CauseName(Done[I].Cause), *CauseName(EBarLossCause::None));

		// 0.5 BW: no slack, no pass.
		Loaded[I] = Run(Unhooked(), Dt, 1.2f, Scenario(180.0f, Constant(0.5f)));
		TestEqual(Rate + TEXT(" 0.5 BW: the pass does not start"), Loaded[I].PassesStarted, 0);
		TestEqual(Rate + TEXT(" ...the bar stays behind the back (blind)"), *PlaceName(Loaded[I].State.Place), *PlaceName(EBarPlace::BehindBack));

		// Back 120 deg off the kite: no pass.
		Turned120[I] = Run(Unhooked(), Dt, 1.2f, Scenario(60.0f, Constant(0.2f)));
		TestEqual(Rate + TEXT(" Back 120 deg off: the pass does not start"), Turned120[I].PassesStarted, 0);
		TestEqual(Rate + TEXT(" ...bar in front"), *PlaceName(Turned120[I].State.Place), *PlaceName(EBarPlace::Front));

		// Started, then 0.8 BW 0.1 s later: lost under load.
		UnderLoad[I] = Run(Unhooked(), Dt, 1.2f, Scenario(180.0f, [](float Mid) { return Mid >= 0.7f + OffGrid ? 0.8f : 0.2f; }));
		TestEqual(Rate + TEXT(" Started, then 0.8 BW: one pass started"), UnderLoad[I].PassesStarted, 1);
		TestEqual(Rate + TEXT(" ...lost under load"), *CauseName(UnderLoad[I].Cause), *CauseName(EBarLossCause::PassUnderLoad));
		TestEqual(Rate + TEXT(" ...never done"), UnderLoad[I].PassesDone, 0);
		TestTrue(FString::Printf(TEXT("%s ...about 0.1 s after the press (lost at %.4f s)"), *Rate, UnderLoad[I].LostAt),
			UnderLoad[I].LostAt >= 0.7f && UnderLoad[I].LostAt <= 0.7f + OffGrid + 2.0f * Dt);

		// The press waits 0.2 s for slack: slack after 0.1 s starts the pass, after 0.3 s does not.
		Buffered[I] = Run(Unhooked(), Dt, 1.2f, Scenario(180.0f, [](float Mid) { return Mid >= 0.7f + OffGrid ? 0.2f : 0.5f; }));
		TestEqual(Rate + TEXT(" Slack 0.1 s after the press: the buffered pass starts"), Buffered[I].PassesStarted, 1);
		Late[I] = Run(Unhooked(), Dt, 1.2f, Scenario(180.0f, [](float Mid) { return Mid >= 0.9f + OffGrid ? 0.2f : 0.5f; }));
		TestEqual(Rate + TEXT(" Slack 0.3 s after the press: no pass"), Late[I].PassesStarted, 0);
	}
	ExpectSameAtBothRates(*this, TEXT("Slack pass"), Done[0], Done[1]);
	ExpectSameAtBothRates(*this, TEXT("Loaded"), Loaded[0], Loaded[1]);
	ExpectSameAtBothRates(*this, TEXT("Back 120 off"), Turned120[0], Turned120[1]);
	ExpectSameAtBothRates(*this, TEXT("Under load"), UnderLoad[0], UnderLoad[1]);
	ExpectSameAtBothRates(*this, TEXT("Buffered"), Buffered[0], Buffered[1]);
	ExpectSameAtBothRates(*this, TEXT("Too late"), Late[0], Late[1]);
	return true;
}

namespace TrickBarTest
{
	/**
	 * A synthetic spin: take off facing the kite, turn SpinDeg (+ backside) evenly over 1.5 s of
	 * air at 0.2 BW, press pass each time the turn passes 130 + 360k deg (k < Presses), land and
	 * ride on for 0.3 s.
	 */
	FRun SpinJump(float SpinDeg, int32 Presses, float Dt, float NoseSide = 1.0f)
	{
		constexpr float AirSeconds = 1.5f;
		const float Abs = FMath::Abs(SpinDeg);
		return Run(Unhooked(), Dt, AirSeconds + 0.3f, [=](float T0, float Step)
		{
			const float Mid = T0 + 0.5f * Step;
			const bool bAir = Mid < AirSeconds;
			const float Turn = SpinDeg * FMath::Min(Mid / AirSeconds, 1.0f);
			FBarInputs In = Inputs(0.2f, Turned(Turn, NoseSide), bAir, NoseSide);
			for (int32 K = 0; K < Presses; ++K)
			{
				const float At = AirSeconds * (130.0f + 360.0f * K) / Abs + OffGrid;
				In.bPassPressed |= During(At, T0, Step);
			}
			return In;
		});
	}

	/** An unhooked signature with one take-off move; the bar fills in the passes and the landing. */
	FTrickSignature WithBar(const FBarState& State, bool bRaley, ETrickInversion* Inversion = nullptr)
	{
		FTrickSignature S;
		S.bRaley = bRaley;
		if (Inversion)
		{
			S.Inversions.Add(*Inversion);
		}
		BarStateMachine::ApplyToSignature(State, S);
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickPassCountsDegrees, "KiteSurf.Trick.PassCountsDegrees",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickPassCountsDegrees::RunTest(const FString& Parameters)
{
	using namespace TrickBarTest;

	struct FCase
	{
		const TCHAR* What;
		float SpinDeg;
		int32 Presses;
		float NoseSide;
		/** Expected: lost for wrapped lines, else these. */
		bool bWrapped;
		int32 HalfTurns;
		ETrickStance Landing;
		/** Name with a pop take-off (nullptr: not checked). */
		const TCHAR* PopName;
	};
	const FCase Cases[] = {
		{ TEXT("Backside 180, no pass"),         180.0f, 0,  1.0f, false, 0, ETrickStance::Blind,    nullptr },
		{ TEXT("Backside 180, one pass"),        180.0f, 1,  1.0f, false, 1, ETrickStance::Blind,    TEXT("Backside 1 to blind") },
		{ TEXT("Backside 360, one pass"),        360.0f, 1,  1.0f, false, 2, ETrickStance::Heelside, TEXT("Backside 3") },
		{ TEXT("Backside 540, one pass"),        540.0f, 1,  1.0f, false, 3, ETrickStance::Blind,    TEXT("Backside 5 to blind") },
		{ TEXT("Backside 720, two passes"),      720.0f, 2,  1.0f, false, 4, ETrickStance::Heelside, TEXT("Backside 7") },
		{ TEXT("Backside 1080, three passes"),  1080.0f, 3,  1.0f, false, 6, ETrickStance::Heelside, TEXT("Backside 10") },
		{ TEXT("Frontside 180, no pass"),       -180.0f, 0,  1.0f, false, 0, ETrickStance::Toeside,  nullptr },
		{ TEXT("Frontside 360, one pass"),      -360.0f, 1,  1.0f, false, 2, ETrickStance::Heelside, TEXT("Frontside 3") },
		{ TEXT("Frontside 540, one pass"),      -540.0f, 1,  1.0f, false, 3, ETrickStance::Toeside,  TEXT("Frontside 5 to toeside") },
		{ TEXT("Backside 360, one pass, nose on the left"), 360.0f, 1, -1.0f, false, 2, ETrickStance::Heelside, TEXT("Backside 3") },
		{ TEXT("Backside 360, no pass"),         360.0f, 0,  1.0f, true,  0, ETrickStance::Heelside, nullptr },
		{ TEXT("Backside 1080, two passes"),    1080.0f, 2,  1.0f, true,  0, ETrickStance::Heelside, nullptr },
	};

	const float Rates[2] = { Dt60, Dt240 };
	for (const FCase& C : Cases)
	{
		FRun Runs[2];
		for (int32 I = 0; I < 2; ++I)
		{
			const float Dt = Rates[I];
			const FString What = FString(C.What) + TEXT(" ") + RateName(Dt);
			const FRun& R = Runs[I] = SpinJump(C.SpinDeg, C.Presses, Dt, C.NoseSide);
			const FBarJumpSummary Sum = BarStateMachine::SummariseJump(R.State);

			TestEqual(What + TEXT(": passes done"), R.PassesDone, C.Presses);
			if (C.bWrapped)
			{
				TestEqual(What + TEXT(": lands with the lines wrapped"), *CauseName(R.Cause), *CauseName(EBarLossCause::LinesWrapped));
				TestFalse(What + TEXT(": not rideable at touchdown"), R.bRideableAtTouchdown);
				TestTrue(What + TEXT(": the summary says wrapped"), Sum.bWrapped);
				continue;
			}
			TestEqual(What + TEXT(": no loss"), *CauseName(R.Cause), *CauseName(EBarLossCause::None));
			TestTrue(What + TEXT(": rideable at touchdown"), R.bRideableAtTouchdown);
			TestFalse(What + TEXT(": not wrapped"), Sum.bWrapped);
			TestEqual(What + TEXT(": landing stance"), *StanceName(Sum.LandingStance), *StanceName(C.Landing));
			TestEqual(What + TEXT(": FTrickPass entries"), Sum.Passes.Num(), C.Presses);
			if (C.Presses > 0)
			{
				// 360 x passes x sense + (W at landing - W at take-off) is the whole spin.
				TestNearlyEqual(What + TEXT(": pass spin (deg)"), Sum.PassSpinDeg, C.SpinDeg, 1.0f);
				TestEqual(What + TEXT(": pass half turns"), Sum.PassHalfTurns, C.HalfTurns);
				int32 Degrees = 0;
				for (const FTrickPass& Pass : Sum.Passes)
				{
					Degrees += Pass.Degrees;
					TestEqual(What + TEXT(": pass sense"), Pass.Sense, C.SpinDeg > 0.0f ? ETrickSense::Backside : ETrickSense::Frontside);
					TestEqual(What + TEXT(": air pass"), Pass.Kind, ETrickPassKind::Air);
				}
				TestEqual(What + TEXT(": the passes carry the whole spin (deg)"), Degrees, C.HalfTurns * 180);
			}
			else
			{
				TestNearlyEqual(What + TEXT(": no pass, the wrap change is the spin (deg)"), Sum.WrapChangeDeg, C.SpinDeg, 1.0f);
			}
			if (C.PopName)
			{
				TestEqual(What + TEXT(": name with a pop"), TrickNaming::Name(WithBar(R.State, false)), FString(C.PopName));
			}
		}
		TestEqual(FString(C.What) + TEXT(": same cause at 60 and 240 Hz"), *CauseName(Runs[0].Cause), *CauseName(Runs[1].Cause));
		TestEqual(FString(C.What) + TEXT(": same passes at 60 and 240 Hz"), Runs[0].PassesDone, Runs[1].PassesDone);
		TestNearlyEqual(FString(C.What) + TEXT(": same wrap at 60 and 240 Hz (deg)"), Runs[0].State.WrapDeg, Runs[1].State.WrapDeg, 1.0f);
	}

	// The same streams with a take-off move: the table names they feed. Spin without a pass comes
	// from the attitude tracker in the game; here it is the wrap change.
	ETrickInversion BackRoll = ETrickInversion::BackRoll;
	ETrickInversion FrontFlip = ETrickInversion::FrontFlip;
	for (const float Dt : Rates)
	{
		const FString Rate = RateName(Dt);
		{
			const FRun R = SpinJump(180.0f, 0, Dt);
			FTrickSignature S = WithBar(R.State, false, &BackRoll);
			S.SpinHalfTurns = 1;
			S.SpinSense = ETrickSense::Backside;
			TestEqual(Rate + TEXT(" Back roll + backside 180, no pass"), TrickNaming::Name(S), FString(TEXT("Back to blind")));
		}
		{
			const FRun R = SpinJump(-180.0f, 0, Dt);
			FTrickSignature S = WithBar(R.State, true);
			S.SpinHalfTurns = 1;
			S.SpinSense = ETrickSense::Frontside;
			TestEqual(Rate + TEXT(" Raley + frontside 180, no pass"), TrickNaming::Name(S), FString(TEXT("Krypt")));
		}
		TestEqual(Rate + TEXT(" Raley + backside 180 air pass"), TrickNaming::Name(WithBar(SpinJump(180.0f, 1, Dt).State, true)), FString(TEXT("Blind judge")));
		TestEqual(Rate + TEXT(" Back roll + backside 360, one pass"), TrickNaming::Name(WithBar(SpinJump(360.0f, 1, Dt).State, false, &BackRoll)), FString(TEXT("KGB")));
		TestEqual(Rate + TEXT(" Raley + frontside 360, one pass"), TrickNaming::Name(WithBar(SpinJump(-360.0f, 1, Dt).State, true)), FString(TEXT("313")));
		TestEqual(Rate + TEXT(" Front flip + frontside 720, two passes"), TrickNaming::Name(WithBar(SpinJump(-720.0f, 2, Dt).State, false, &FrontFlip)), FString(TEXT("Slim 7")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickWrapHeldAlongLine, "KiteSurf.Trick.WrapHeldAlongLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickWrapHeldAlongLine::RunTest(const FString& Parameters)
{
	using namespace TrickBarTest;
	const FVector D = KiteDir();

	const float Rates[2] = { Dt60, Dt240 };
	for (const float Dt : Rates)
	{
		const FString Rate = RateName(Dt);

		// A full roll about the line (line 45 deg off Up the whole time): the line does not move
		// in the body frame, so W does not change.
		{
			FBarState S = Unhooked();
			float MaxAbsW = 0.0f;
			float MinUpDot = 1.0f;
			const int32 Steps = FMath::RoundToInt(1.0f / Dt);
			for (int32 K = 0; K <= Steps; ++K)
			{
				const FQuat Body = FQuat(D, FMath::DegreesToRadians(360.0f * K / Steps));
				BarStateMachine::Step(S, Inputs(0.2f, Body, true), FBarTunables(), Dt);
				MaxAbsW = FMath::Max(MaxAbsW, FMath::Abs(S.WrapDeg));
				MinUpDot = FMath::Min(MinUpDot, Body.GetAxisZ().Z);
			}
			TestTrue(FString::Printf(TEXT("%s The roll turns the body far from upright (min up.z %.2f)"), *Rate, MinUpDot), MinUpDot < 0.1f);
			TestTrue(FString::Printf(TEXT("%s A 360 roll about the line leaves W at 0 (max |W| %.4f deg)"), *Rate, MaxAbsW), MaxAbsW < 0.01f);
		}

		// The line along the body (15 deg off Up, a raley or S-bend): a 360 about the body's long
		// axis sweeps the line's azimuth all the way round, but W is held.
		{
			FBarState S = Unhooked();
			const FQuat Tilted = FQuat(FVector::YAxisVector, FMath::DegreesToRadians(30.0f));
			const FVector LongAxis = Tilted.GetAxisZ();
			TestTrue(TEXT("The tilted body's Up is within 25.8 deg of the line"), FVector::DotProduct(LongAxis, D) > FBarTunables().WrapHoldUpDot);
			const int32 Steps = FMath::RoundToInt(1.0f / Dt);
			// Seed facing the kite, tilt back over 0.25 s, spin 360 about the long axis over 1 s, tilt up again.
			const int32 TiltSteps = FMath::RoundToInt(0.25f / Dt);
			for (int32 K = 0; K <= TiltSteps; ++K)
			{
				BarStateMachine::Step(S, Inputs(0.2f, FQuat::Slerp(FQuat::Identity, Tilted, float(K) / TiltSteps), true), FBarTunables(), Dt);
			}
			const float WBefore = S.WrapDeg;
			float AzimuthSwept = 0.0f;
			float LastAzimuth = BarStateMachine::LineAzimuthDeg(Tilted, D);
			float MaxDrift = 0.0f;
			for (int32 K = 1; K <= Steps; ++K)
			{
				const FQuat Body = FQuat(LongAxis, FMath::DegreesToRadians(360.0f * K / Steps)) * Tilted;
				BarStateMachine::Step(S, Inputs(0.2f, Body, true), FBarTunables(), Dt);
				const float Azimuth = BarStateMachine::LineAzimuthDeg(Body, D);
				AzimuthSwept += FMath::FindDeltaAngleDegrees(LastAzimuth, Azimuth);
				LastAzimuth = Azimuth;
				MaxDrift = FMath::Max(MaxDrift, FMath::Abs(S.WrapDeg - WBefore));
			}
			for (int32 K = 0; K <= TiltSteps; ++K)
			{
				BarStateMachine::Step(S, Inputs(0.2f, FQuat::Slerp(Tilted, FQuat::Identity, float(K) / TiltSteps), true), FBarTunables(), Dt);
			}
			TestTrue(FString::Printf(TEXT("%s The line's azimuth went all the way round (%.1f deg)"), *Rate, AzimuthSwept), FMath::Abs(AzimuthSwept) > 300.0f);
			TestTrue(FString::Printf(TEXT("%s W held through the spin (drift %.4f deg)"), *Rate, MaxDrift), MaxDrift < 0.01f);
			TestNearlyEqual(Rate + TEXT(" W back at 0 once the line drops in front (deg)"), S.WrapDeg, 0.0f, 0.5f);
			TestEqual(Rate + TEXT(" ...bar in front"), *PlaceName(S.Place), *PlaceName(EBarPlace::Front));
		}

		// Contrast: a 360 about world Up with the kite at 45 deg winds the lines once round.
		{
			FBarState S = Unhooked();
			const int32 Steps = FMath::RoundToInt(1.0f / Dt);
			for (int32 K = 0; K <= Steps; ++K)
			{
				BarStateMachine::Step(S, Inputs(0.2f, Turned(360.0f * K / Steps), true), FBarTunables(), Dt);
			}
			TestNearlyEqual(Rate + TEXT(" A backside 360 spin about Up winds W to +360 (deg)"), S.WrapDeg, 360.0f, 0.5f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickSurfacePassGrace, "KiteSurf.Trick.SurfacePassGrace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickSurfacePassGrace::RunTest(const FString& Parameters)
{
	using namespace TrickBarTest;
	constexpr float Touchdown = 1.0f + OffGrid;

	/** Turn backside 180 in the first 0.5 s of air at 0.2 BW, press pass Lead s before touchdown (negative: after), ride on. */
	const auto Landing = [=](float Lead, float Dt, const FBarTunables& T)
	{
		return Run(Unhooked(), Dt, Touchdown + 0.8f, [=](float T0, float Step)
		{
			const float Mid = T0 + 0.5f * Step;
			FBarInputs In = Inputs(0.2f, Turned(180.0f * FMath::Min(Mid / 0.5f, 1.0f)), Mid < Touchdown);
			In.bPassPressed = During(Touchdown - Lead + 0.001f, T0, Step);
			return In;
		}, T);
	};

	FBarTunables Slow;
	Slow.PassDurationSeconds = 0.45f;

	FRun Surface[2], Air[2], Unfinished[2], JustInTime[2], OnWater[2];
	const float Rates[2] = { Dt60, Dt240 };
	for (int32 I = 0; I < 2; ++I)
	{
		const float Dt = Rates[I];
		const FString Rate = RateName(Dt);

		// Started 0.1 s before touchdown, 0.15 s of it on the water: a surface pass.
		Surface[I] = Landing(0.1f, Dt, FBarTunables());
		TestEqual(Rate + TEXT(" Started 0.1 s before touchdown: done"), Surface[I].PassesDone, 1);
		TestEqual(Rate + TEXT(" ...as a surface pass"), *KindName(Surface[I].LastPassKind), *KindName(ETrickPassKind::Surface));
		TestTrue(Rate + TEXT(" ...after the touchdown"), Surface[I].PassDoneAt > Touchdown);
		TestEqual(Rate + TEXT(" ...no loss"), *CauseName(Surface[I].Cause), *CauseName(EBarLossCause::None));
		if (Surface[I].State.JumpPasses.Num() == 1)
		{
			TestEqual(Rate + TEXT(" ...recorded with the jump as a surface pass"), *KindName(Surface[I].State.JumpPasses[0].Kind), *KindName(ETrickPassKind::Surface));
		}
		const FBarJumpSummary Sum = BarStateMachine::SummariseJump(Surface[I].State);
		TestEqual(Rate + TEXT(" ...a backside 180 pass landing blind"), *StanceName(Sum.LandingStance), *StanceName(ETrickStance::Blind));
		TestEqual(Rate + TEXT(" ...180 deg"), Sum.Passes.Num() == 1 ? Sum.Passes[0].Degrees : -1, 180);

		// Started 0.3 s before touchdown: done in the air.
		Air[I] = Landing(0.3f, Dt, FBarTunables());
		TestEqual(Rate + TEXT(" Started 0.3 s before touchdown: an air pass"), *KindName(Air[I].LastPassKind), *KindName(ETrickPassKind::Air));

		// Started 0.05 s before touchdown with 0.4 s still to go: more than the 0.3 s grace on the water.
		Unfinished[I] = Landing(0.05f, Dt, Slow);
		TestEqual(Rate + TEXT(" 0.4 s of pass left at touchdown: lost"), *CauseName(Unfinished[I].Cause), *CauseName(EBarLossCause::PassUnfinished));
		TestEqual(Rate + TEXT(" ...never done"), Unfinished[I].PassesDone, 0);
		TestTrue(FString::Printf(TEXT("%s ...0.3 s after touchdown (lost %.4f s after)"), *Rate, Unfinished[I].LostAt - Touchdown),
			Unfinished[I].LostAt - Touchdown >= 0.3f - Dt && Unfinished[I].LostAt - Touchdown <= 0.3f + 2.0f * Dt);
		TestEqual(Rate + TEXT(" ...landing cause PassUnfinished"), BarStateMachine::LandingCauseOf(Unfinished[I].Cause), ELandingCause::PassUnfinished);

		// The same 0.05 s lead with the normal 0.25 s pass: 0.2 s on the water, a surface pass.
		JustInTime[I] = Landing(0.05f, Dt, FBarTunables());
		TestEqual(Rate + TEXT(" 0.2 s of pass left at touchdown: a surface pass"), *KindName(JustInTime[I].LastPassKind), *KindName(ETrickPassKind::Surface));

		// Riding blind, a pass started 0.2 s after touchdown is all on the water and still completes:
		// the grace counts the pass's time on the water, not the time since touchdown.
		OnWater[I] = Landing(-0.2f, Dt, FBarTunables());
		TestEqual(Rate + TEXT(" A pass started on the water: a surface pass"), *KindName(OnWater[I].LastPassKind), *KindName(ETrickPassKind::Surface));
		TestEqual(Rate + TEXT(" ...no loss"), *CauseName(OnWater[I].Cause), *CauseName(EBarLossCause::None));
	}
	ExpectSameAtBothRates(*this, TEXT("Surface pass"), Surface[0], Surface[1]);
	ExpectSameAtBothRates(*this, TEXT("Air pass"), Air[0], Air[1]);
	ExpectSameAtBothRates(*this, TEXT("Unfinished pass"), Unfinished[0], Unfinished[1]);
	ExpectSameAtBothRates(*this, TEXT("Just in time"), JustInTime[0], JustInTime[1]);
	ExpectSameAtBothRates(*this, TEXT("On the water"), OnWater[0], OnWater[1]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickHookOnlyOnWater, "KiteSurf.Trick.HookOnlyOnWater",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickHookOnlyOnWater::RunTest(const FString& Parameters)
{
	using namespace TrickBarTest;
	const FBarTunables T;
	const float Dt = Dt240;

	const auto Press = [&](FBarState& S, bool bAirborne, const FQuat& Body = FQuat::Identity, bool bRideable = true)
	{
		FBarInputs In = Inputs(0.5f, Body, bAirborne);
		In.bOnWaterRideable = !bAirborne && bRideable;
		In.bHookPressed = true;
		return BarStateMachine::Step(S, In, T, Dt);
	};

	FBarState S;
	TestTrue(TEXT("Starts hooked in"), S.bHooked);

	FBarEvents E = Press(S, true);
	TestTrue(TEXT("Airborne: an unhook press is ignored"), S.bHooked && !E.bUnhooked);

	E = Press(S, false);
	TestTrue(TEXT("On the water: the press unhooks"), !S.bHooked && E.bUnhooked);

	E = Press(S, true);
	TestTrue(TEXT("Airborne: a hook press is ignored"), !S.bHooked && !E.bHooked);

	E = Press(S, false, FQuat::Identity, false);
	TestTrue(TEXT("On the water but crashed: ignored"), !S.bHooked && !E.bHooked);

	E = Press(S, false);
	TestTrue(TEXT("On the water again: the press hooks in"), S.bHooked && E.bHooked);

	// Hooked, the wrap stays 0 whatever the body does.
	for (int32 K = 0; K <= 60; ++K)
	{
		BarStateMachine::Step(S, Inputs(0.5f, Turned(180.0f * K / 60.0f), true), T, Dt);
	}
	TestEqual(TEXT("Hooked, a half turn leaves W at 0 (deg)"), S.WrapDeg, 0.0f);

	// Unhooked and blind on the water (bar behind the back): no hooking in until back in front.
	FBarState Blind = Unhooked();
	for (int32 K = 0; K <= 60; ++K)
	{
		BarStateMachine::Step(Blind, Inputs(0.5f, Turned(180.0f * K / 60.0f), false), T, Dt);
	}
	TestEqual(TEXT("Riding blind: bar behind the back"), *PlaceName(Blind.Place), *PlaceName(EBarPlace::BehindBack));
	E = Press(Blind, false, Turned(180.0f));
	TestTrue(TEXT("Riding blind: a hook press is ignored"), !Blind.bHooked && !E.bHooked);

	// Toeside (lines round the front, bar in front): also no hooking in with the lines round the body.
	FBarState Toeside = Unhooked();
	for (int32 K = 0; K <= 60; ++K)
	{
		BarStateMachine::Step(Toeside, Inputs(0.5f, Turned(-180.0f * K / 60.0f), false), T, Dt);
	}
	TestEqual(TEXT("Riding toeside: bar in front"), *PlaceName(Toeside.Place), *PlaceName(EBarPlace::Front));
	E = Press(Toeside, false, Turned(-180.0f));
	TestTrue(TEXT("Riding toeside: a hook press is ignored"), !Toeside.bHooked && !E.bHooked);

	// One hand off: no hooking in.
	FBarState OneHand = Unhooked();
	FBarInputs Release = Inputs(0.5f, FQuat::Identity, false);
	Release.bReleaseBack = true;
	BarStateMachine::Step(OneHand, Release, T, Dt);
	TestEqual(TEXT("Back hand off: front hand only"), OneHand.Hands, EBarHands::FrontOnly);
	E = Press(OneHand, false);
	TestTrue(TEXT("One hand off: a hook press is ignored"), !OneHand.bHooked && !E.bHooked);

	// A lost bar stays lost.
	FBarState Lost = Unhooked();
	for (int32 K = 0; K < 60; ++K)
	{
		BarStateMachine::Step(Lost, Inputs(2.0f, FQuat::Identity, false), T, Dt);
	}
	TestEqual(TEXT("Over the grip limit: lost"), *PlaceName(Lost.Place), *PlaceName(EBarPlace::Lost));
	E = Press(Lost, false);
	TestTrue(TEXT("Lost: a hook press is ignored"), !Lost.bHooked && !E.bHooked && Lost.Place == EBarPlace::Lost);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
