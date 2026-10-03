#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "School/LessonCatalog.h"
#include "School/LessonEvaluator.h"
#include "Tests/SchoolTestScenes.h"

#if WITH_DEV_AUTOMATION_TESTS

// Named, not anonymous, so a unity build cannot merge these with another file's helpers.
namespace SchoolCatalogTest
{
	using namespace SchoolTestScenes;

	/** Chapters A and B of docs/tutorials.md section 2: id, prerequisites, required feature. */
	struct FExpectedLesson
	{
		const TCHAR* Id;
		TArray<FName> Requires;
		ELessonFeature Feature;
	};

	TArray<FExpectedLesson> ExpectedChaptersAB()
	{
		return {
			{ TEXT("A1"), {}, ELessonFeature::None },
			{ TEXT("A2"), { TEXT("A1") }, ELessonFeature::None },
			{ TEXT("A3"), { TEXT("A2") }, ELessonFeature::None },
			{ TEXT("A4"), { TEXT("A3") }, ELessonFeature::None },
			{ TEXT("A5"), { TEXT("A4") }, ELessonFeature::None },
			{ TEXT("A6"), { TEXT("A5") }, ELessonFeature::ToesideRiding },
			{ TEXT("A7"), { TEXT("A5") }, ELessonFeature::None },
			{ TEXT("B1"), { TEXT("A4") }, ELessonFeature::None },
			{ TEXT("B2"), { TEXT("B1") }, ELessonFeature::None },
			{ TEXT("B3"), { TEXT("B2") }, ELessonFeature::None },
			{ TEXT("B4"), { TEXT("B3") }, ELessonFeature::Grabs },
			{ TEXT("B5"), { TEXT("B3"), TEXT("A5") }, ELessonFeature::None },
			{ TEXT("B6"), { TEXT("B5") }, ELessonFeature::None },
		};
	}

	// --- Synthetic rides for each lesson: one that passes, and one per fault rule that fails. ---

	/** Planing on tack +1 with the kite at 45 deg for Seconds. */
	FSchoolScene SteadyRide(float Seconds, TFunction<void(FLessonSample&, float)> Change = nullptr)
	{
		return BuildRide(0.0f, Seconds, [&Change](float T)
		{
			FLessonSample S = Riding(T);
			if (Change)
			{
				Change(S, T);
			}
			return S;
		});
	}

	/** Tack +1 until 5 s, then -1, up to 9 s. */
	FSchoolScene Transition(TFunction<void(FLessonSample&, float)> Change = nullptr)
	{
		return SteadyRide(9.0f, [&Change](FLessonSample& S, float T)
		{
			S.Tack = T < 5.0f ? 1 : -1;
			S.KiteClockDeg = (T - 4.5f) * 20.0f; // the kite crosses 12 half a second before the board turns
			if (Change)
			{
				Change(S, T);
			}
		});
	}

	/** At 85 deg with a dive every 3 s from 0.5 s to Depth, each Duration long, ending EndT. */
	FSchoolScene Dives(float Depth, float Duration, float EndT)
	{
		return SteadyRide(EndT, [=](FLessonSample& S, float T)
		{
			const float Phase = FMath::Fmod(T - 0.5f, 3.0f) / Duration;
			S.KiteElevationDeg = (T >= 0.5f && Phase < 1.0f) ? 85.0f - (85.0f - Depth) * FMath::Sin(PI * Phase) : 85.0f;
		});
	}

	FJumpScene PopScene()
	{
		FJumpScene P;
		P.HeightM = 0.7f;
		P.Airtime = 0.7f;
		P.bPopped = true;
		P.KiteLowDeg = 60.0f;
		P.KiteTopDeg = 60.0f;
		P.DiveToDeg = 60.0f;
		return P;
	}

	FJumpScene LandingDiveScene()
	{
		FJumpScene P;
		P.HeightM = 4.0f;
		P.Airtime = 1.8f;
		return P;
	}

	FJumpScene GrabScene()
	{
		FJumpScene P;
		P.GrabHoldS = 0.7f;
		P.ClockInAir = 5.0f;
		return P;
	}

	FJumpScene TransitionJumpScene()
	{
		FJumpScene P;
		P.HeadingAfter = 270.0f;
		P.ClockAtLanding = 10.0f;
		return P;
	}

	/** A jump scene graded down by a landing evaluator cause. */
	FJumpScene WithCause(FJumpScene P, ELandingCause Cause)
	{
		P.Cause = Cause;
		P.Grade = ELandingGrade::Sketchy;
		return P;
	}

	/** The passing ride for a lesson. */
	bool GoodScene(FName Id, FSchoolScene& Out)
	{
		const FString S = Id.ToString();
		if (S == TEXT("A1")) { Out = Dives(45.0f, 1.0f, 4.0f); return true; }
		if (S == TEXT("A2")) { Out = SteadyRide(10.0f); return true; }
		if (S == TEXT("A3")) { Out = SteadyRide(22.0f, [](FLessonSample& L, float) { L.SpeedMS = 15.0f * 0.5144f; }); return true; }
		if (S == TEXT("A4")) { Out = SteadyRide(20.0f, [](FLessonSample& L, float T) { L.UpwindM = 3.0f * T; }); return true; }
		if (S == TEXT("A5")) { Out = Transition([](FLessonSample& L, float T) { L.SpeedMS = T < 5.0f ? 8.0f : 7.0f; }); return true; }
		if (S == TEXT("A6")) { Out = SteadyRide(15.0f, [](FLessonSample& L, float) { L.bToeside = true; }); return true; }
		if (S == TEXT("A7")) { Out = Transition(); return true; }
		if (S == TEXT("B1")) { Out = BuildJump(PopScene()); return true; }
		if (S == TEXT("B2")) { Out = BuildJump(FJumpScene()); return true; }
		if (S == TEXT("B3")) { Out = BuildJump(LandingDiveScene()); return true; }
		if (S == TEXT("B4")) { Out = BuildJump(GrabScene()); return true; }
		if (S == TEXT("B5")) { Out = BuildJump(TransitionJumpScene()); return true; }
		if (S == TEXT("B6"))
		{
			Out = Transition([](FLessonSample& L, float T)
			{
				L.CompletedLoops = T < 4.5f ? 0 : 1;
				L.KiteElevationDeg = 45.0f - 15.0f * FMath::Max(0.0f, 1.0f - FMath::Abs(T - 4.5f));
			});
			return true;
		}
		return false;
	}

	/** The jump scene a jump lesson's failing rides start from. */
	FJumpScene JumpBase(const FString& Lesson)
	{
		if (Lesson == TEXT("B1")) { return PopScene(); }
		if (Lesson == TEXT("B3")) { return LandingDiveScene(); }
		if (Lesson == TEXT("B4")) { return GrabScene(); }
		if (Lesson == TEXT("B5")) { return TransitionJumpScene(); }
		return FJumpScene();
	}

	/** A failing ride for one fault rule of a lesson. */
	bool BadScene(FName LessonId, FName FaultId, FSchoolScene& Out)
	{
		const FString L = LessonId.ToString();
		const FString F = FaultId.ToString();
		const FString Key = L + TEXT(".") + F;

		// Landing evaluator causes, in every jump lesson that lists them.
		if (F.StartsWith(TEXT("Cause")))
		{
			const UEnum* Enum = StaticEnum<ELandingCause>();
			const int64 Value = Enum->GetValueByNameString(F.RightChop(5));
			if (Value == INDEX_NONE)
			{
				return false;
			}
			Out = BuildJump(WithCause(JumpBase(L), static_cast<ELandingCause>(Value)));
			return true;
		}

		if (Key == TEXT("A1.TooDeep")) { Out = Dives(25.0f, 1.0f, 4.0f); return true; }
		if (Key == TEXT("A1.TooSlowBack")) { Out = SteadyRide(4.0f, [](FLessonSample& S, float T) { S.KiteElevationDeg = T < 1.0f ? 85.0f : 50.0f; }); return true; }
		if (Key == TEXT("A2.BoardSquare")) { Out = SteadyRide(10.0f, [](FLessonSample& S, float T) { S.KiteBearingDeg = T < 6.0f ? 45.0f : 90.0f; }); return true; }
		if (Key == TEXT("A2.Nosedive")) { Out = SteadyRide(10.0f, [](FLessonSample& S, float T) { S.bFallen = T > 9.0f; }); return true; }
		if (Key == TEXT("A3.KiteParkedHigh")) { Out = SteadyRide(22.0f, [](FLessonSample& S, float) { S.KiteElevationDeg = 65.0f; }); return true; }
		if (Key == TEXT("A4.CarvedTooSoon"))
		{
			Out = SteadyRide(20.0f, [](FLessonSample& S, float T) { S.BoardState = EBoardState::Displacement; S.SpeedMS = 3.0f; S.UpwindM = 0.5f * T; });
			return true;
		}
		if (Key == TEXT("A4.SpeedCollapsed"))
		{
			Out = SteadyRide(20.0f, [](FLessonSample& S, float T) { S.SpeedMS = T < 16.0f ? 8.0f : 8.0f - (T - 16.0f); S.UpwindM = 3.0f * T; });
			return true;
		}
		if (Key == TEXT("A5.SankAfter"))
		{
			Out = Transition([](FLessonSample& S, float T)
			{
				if (T >= 5.0f) { S.BoardState = EBoardState::Displacement; S.SpeedMS = 3.0f; }
			});
			return true;
		}
		if (Key == TEXT("A6.FrontHandSteering"))
		{
			Out = SteadyRide(15.0f, [](FLessonSample& S, float T) { S.bToeside = true; S.KiteElevationDeg = T > 13.0f ? 20.0f : 45.0f; });
			return true;
		}
		if (Key == TEXT("A7.KiteEarly"))
		{
			Out = Transition([](FLessonSample& S, float T) { S.KiteClockDeg = (T - 3.0f) * 20.0f; S.BoardState = T >= 5.0f && T < 6.0f ? EBoardState::Displacement : EBoardState::Planing; });
			return true;
		}
		if (Key == TEXT("A7.KiteLate"))
		{
			Out = Transition([](FLessonSample& S, float T) { S.KiteClockDeg = (T - 5.5f) * 20.0f; S.BoardState = T >= 5.0f && T < 6.0f ? EBoardState::Displacement : EBoardState::Planing; });
			return true;
		}
		if (Key == TEXT("B1.KiteRose")) { FJumpScene P = PopScene(); P.PopRiseDeg = 15.0f; Out = BuildJump(P); return true; }
		if (Key == TEXT("B1.Skipped")) { FJumpScene P = PopScene(); P.TakeoffSpeedMS = 11.0f; P.HeightM = 0.3f; Out = BuildJump(P); return true; }
		if (Key == TEXT("B2.SheetedInClimbing")) { FJumpScene P; P.BarClimbing = 0.9f; P.HeightM = 2.5f; Out = BuildJump(P); return true; }
		if (Key == TEXT("B2.EdgeLost")) { FJumpScene P; P.EdgeDropBeforeS = 0.3f; P.HeightM = 0.6f; Out = BuildJump(P); return true; }
		if (Key == TEXT("B3.FrontStall")) { FJumpScene P = LandingDiveScene(); P.KiteDownwindAtLanding = -2.0f; P.Grade = ELandingGrade::Sketchy; Out = BuildJump(P); return true; }
		if (Key == TEXT("B3.DivedEarly")) { FJumpScene P = LandingDiveScene(); P.DiveToDeg = 40.0f; P.Grade = ELandingGrade::Sketchy; Out = BuildJump(P); return true; }
		if (Key == TEXT("B3.DiveTooSoon")) { FJumpScene P = LandingDiveScene(); P.DiveLeadS = 1.4f; P.DiveToDeg = 50.0f; Out = BuildJump(P); return true; }
		if (Key == TEXT("B3.DivedLate")) { FJumpScene P = LandingDiveScene(); P.DiveLeadS = 0.05f; Out = BuildJump(P); return true; }
		if (Key == TEXT("B4.KiteDrifted")) { FJumpScene P = GrabScene(); P.ClockInAir = 25.0f; Out = BuildJump(P); return true; }
		if (Key == TEXT("B5.KitePast12")) { FJumpScene P = TransitionJumpScene(); P.ClockAtLanding = 50.0f; P.Grade = ELandingGrade::Sketchy; Out = BuildJump(P); return true; }
		if (Key == TEXT("B5.TooFast")) { FJumpScene P = TransitionJumpScene(); P.TakeoffSpeedMS = 11.0f; P.HeadingAfter = 150.0f; Out = BuildJump(P); return true; }
		if (Key == TEXT("B6.LetGoMidLoop"))
		{
			Out = Transition([](FLessonSample& S, float T)
			{
				S.CompletedLoops = 0;
				S.KiteElevationDeg = 45.0f - 35.0f * FMath::Max(0.0f, 1.0f - FMath::Abs(T - 4.5f));
			});
			return true;
		}
		return false;
	}

	bool IsJumpObjective(const FLessonObjective& O)
	{
		return LessonEval::GetMetricSource(O.Metric) == ELessonMetricSource::Jump;
	}

	/** The objective is met by the scene's newest event or hold, from fresh progress. */
	bool PassMet(const FLessonObjective& O, const FSchoolScene& Scene)
	{
		const FObjectiveResult R = EvaluateOnce(O, Scene);
		return R.bCounted || R.bPassed;
	}
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolCatalogIntegrity, "KiteSurf.School.CatalogIntegrity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfSchoolCatalogIntegrity::RunTest(const FString& Parameters)
{
	using namespace SchoolCatalogTest;
	const TArray<FLessonDef>& All = LessonCatalog::GetAll();

	for (const FString& Problem : LessonCatalog::Validate(All))
	{
		AddError(TEXT("Catalogue problem: ") + Problem);
	}

	const TArray<FExpectedLesson> Expected = ExpectedChaptersAB();
	TestEqual(TEXT("Chapters A and B have 13 lessons (docs/tutorials.md section 2)"), All.Num(), Expected.Num());
	for (int32 I = 0; I < FMath::Min(All.Num(), Expected.Num()); ++I)
	{
		const FLessonDef& L = All[I];
		const FExpectedLesson& E = Expected[I];
		const FString Id = E.Id;
		TestTrue(Id + TEXT(": in order"), L.Id == FName(E.Id));
		TestTrue(Id + TEXT(": chapter"), L.Chapter == FName(*Id.Left(1)));
		TestTrue(Id + TEXT(": prerequisites as section 2"), L.Requires == E.Requires);
		TestEqual(Id + TEXT(": required feature"), static_cast<int32>(L.RequiredFeature), static_cast<int32>(E.Feature));
		TestTrue(Id + TEXT(": available unless it needs an unbuilt feature"), LessonCatalog::IsAvailable(L) == (E.Feature == ELessonFeature::None));
		TestTrue(Id + TEXT(": flat water"), L.Setup.Map == FName(TEXT("L_FlatWater")));
		TestTrue(Id + TEXT(": no demonstration yet"), L.DemoId.IsNone());
		TestTrue(Id + TEXT(": has a pass objective"), L.Pass.IsSet());
		TestTrue(Id + TEXT(": has a fault line"), L.Faults.Num() > 0);
		TestTrue(Id + TEXT(": has at least one step"), L.Steps.Num() > 0);
		const float Wind = L.Setup.WindKnots;
		if (Id.StartsWith(TEXT("A")))
		{
			TestTrue(Id + TEXT(": chapter A wind 10 to 14 kn"), Wind >= 10.0f && Wind <= 14.0f);
		}
		else
		{
			TestTrue(Id + TEXT(": chapter B wind 10 to 16 kn (B6 very light)"), Wind >= 10.0f && Wind <= 16.0f);
		}
		for (const FLessonFault& F : L.Faults)
		{
			TestFalse(Id + TEXT(" fault ") + F.Id.ToString() + TEXT(": has a feedback line"), F.Feedback.IsEmpty());
		}
	}

	TestNotNull(TEXT("Find B3"), LessonCatalog::Find(TEXT("B3")));
	TestNull(TEXT("Find of a lesson not in the catalogue"), LessonCatalog::Find(TEXT("F1")));
	TestTrue(TEXT("No feature: built"), LessonCatalog::IsFeatureBuilt(ELessonFeature::None));
	TestFalse(TEXT("Toeside riding: not built"), LessonCatalog::IsFeatureBuilt(ELessonFeature::ToesideRiding));
	TestFalse(TEXT("Grabs: not built"), LessonCatalog::IsFeatureBuilt(ELessonFeature::Grabs));

	// The section 2 feedback lines appear word for word.
	auto HasLine = [](const TCHAR* Id, const TCHAR* Line)
	{
		const FLessonDef* L = LessonCatalog::Find(Id);
		return L && L->Faults.ContainsByPredicate([Line](const FLessonFault& F) { return F.Feedback.ToString() == Line; });
	};
	TestTrue(TEXT("B2 sheeting line"), HasLine(TEXT("B2"), TEXT("Bar out while it climbs, in at 12")));
	TestTrue(TEXT("B2 edge line"), HasLine(TEXT("B2"), TEXT("Hold the edge until take-off")));
	TestTrue(TEXT("B3 early line"), HasLine(TEXT("B3"), TEXT("Dived early: the kite was too low to catch you")));
	TestTrue(TEXT("B3 late line"), HasLine(TEXT("B3"), TEXT("Dived late: no pull, you sank")));
	TestTrue(TEXT("B3 front stall line"), HasLine(TEXT("B3"), TEXT("The kite overflew")));
	TestTrue(TEXT("B1 kite line"), HasLine(TEXT("B1"), TEXT("You lifted the kite: pop with the board")));
	TestTrue(TEXT("A4 plane first line"), HasLine(TEXT("A4"), TEXT("Plane first, then carve upwind")));
	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolCatalogValidate, "KiteSurf.School.CatalogValidateCatchesProblems",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfSchoolCatalogValidate::RunTest(const FString& Parameters)
{
	const TArray<FLessonDef>& All = LessonCatalog::GetAll();
	auto ProblemsWith = [&All](TFunctionRef<void(TArray<FLessonDef>&)> Break)
	{
		TArray<FLessonDef> Copy = All;
		Break(Copy);
		return LessonCatalog::Validate(Copy);
	};
	auto Mentions = [](const TArray<FString>& Problems, const TCHAR* Text)
	{
		return Problems.ContainsByPredicate([Text](const FString& P) { return P.Contains(Text); });
	};

	TestTrue(TEXT("Duplicate id"), Mentions(ProblemsWith([](TArray<FLessonDef>& L) { L[1].Id = L[0].Id; }), TEXT("duplicate id")));
	TestTrue(TEXT("Missing prerequisite"), Mentions(ProblemsWith([](TArray<FLessonDef>& L) { L[2].Requires.Add(TEXT("Z9")); }), TEXT("requires missing lesson Z9")));
	TestTrue(TEXT("Self prerequisite"), Mentions(ProblemsWith([](TArray<FLessonDef>& L) { L[2].Requires.Add(L[2].Id); }), TEXT("requires itself")));
	TestTrue(TEXT("Cycle"), Mentions(ProblemsWith([](TArray<FLessonDef>& L) { L[0].Requires.Add(TEXT("B6")); }), TEXT("cycle")));
	TestTrue(TEXT("No pass objective"), Mentions(ProblemsWith([](TArray<FLessonDef>& L) { L[3].Pass = FLessonObjective(); }), TEXT("pass: no objective")));
	TestTrue(TEXT("No fault line"), Mentions(ProblemsWith([](TArray<FLessonDef>& L) { L[3].Faults.Reset(); }), TEXT("no fault line")));
	TestTrue(TEXT("Empty feedback"), Mentions(ProblemsWith([](TArray<FLessonDef>& L) { L[3].Faults[0].Feedback = FText::GetEmpty(); }), TEXT("no feedback")));
	TestTrue(TEXT("Another map"), Mentions(ProblemsWith([](TArray<FLessonDef>& L) { L[3].Setup.Map = TEXT("L_OpenWater"); }), TEXT("L_FlatWater")));
	TestTrue(TEXT("Wrong chapter"), Mentions(ProblemsWith([](TArray<FLessonDef>& L) { L[3].Chapter = TEXT("B"); }), TEXT("not in its chapter")));
	TestTrue(TEXT("Hold longer than the telemetry"), Mentions(ProblemsWith([](TArray<FLessonDef>& L) { L[2].Pass.WindowSeconds = 60.0f; }), TEXT("hold longer")));
	TestTrue(TEXT("Window past the newest sample"), Mentions(ProblemsWith([](TArray<FLessonDef>& L)
	{
		L[3].Faults[0].Measure.ToSeconds = 1.0f;
	}), TEXT("past the newest sample")));
	TestTrue(TEXT("Unreadable fault measure"), Mentions(ProblemsWith([](TArray<FLessonDef>& L)
	{
		L[3].Faults[0].Measure = LessonRules::Jump(ELessonMetric::UpwindGain);
	}), TEXT("cannot be read")));
	TestTrue(TEXT("Stars that cannot reach two"), Mentions(ProblemsWith([](TArray<FLessonDef>& L)
	{
		L[6].Stars.HigherBar.Reset();
		L[6].Setup.Assists = FLessonAssists();
	}), TEXT("cannot be reached")));
	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolCatalogFaultRules, "KiteSurf.School.CatalogFaultRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfSchoolCatalogFaultRules::RunTest(const FString& Parameters)
{
	using namespace SchoolCatalogTest;
	int32 RulesChecked = 0;
	int32 RulesTotal = 0;
	for (const FLessonDef& L : LessonCatalog::GetAll())
	{
		const FString Id = L.Id.ToString();
		RulesTotal += L.Faults.Num();

		FSchoolScene Good;
		if (!GoodScene(L.Id, Good))
		{
			AddError(Id + TEXT(": no passing ride in the test"));
			continue;
		}
		TestTrue(Id + TEXT(": the passing ride meets the pass objective"), PassMet(L.Pass, Good));
		const FLessonFault* None = LessonEval::DiagnoseFault(L.Faults, Good.Telemetry, Good.Jump, Good.Extras);
		TestNull(Id + TEXT(": no fault on the passing ride") + (None ? TEXT(" (got ") + None->Id.ToString() + TEXT(")") : FString()), None);
		for (const FLessonFault& F : L.Faults)
		{
			TestFalse(Id + TEXT(" ") + F.Id.ToString() + TEXT(": does not fire on the passing ride"), RuleMatches(F, Good));
		}

		for (const FLessonFault& F : L.Faults)
		{
			const FString Where = Id + TEXT(" ") + F.Id.ToString();
			FSchoolScene Bad;
			if (!BadScene(L.Id, F.Id, Bad))
			{
				AddError(Where + TEXT(": no failing ride in the test"));
				continue;
			}
			++RulesChecked;
			TestTrue(Where + TEXT(": fires on its failing ride"), RuleMatches(F, Bad));
			const FLessonFault* Shown = LessonEval::DiagnoseFault(L.Faults, Bad.Telemetry, Bad.Jump, Bad.Extras);
			TestNotNull(Where + TEXT(": a line is shown"), Shown);
			if (Shown)
			{
				TestTrue(Where + TEXT(": the line shown is this rule or one of higher priority (got ") + Shown->Id.ToString() + TEXT(")"),
					Shown == &F || Shown->Priority > F.Priority || (Shown->Priority == F.Priority && Shown->Feedback.EqualTo(F.Feedback)));
			}
			if (IsJumpObjective(L.Pass))
			{
				TestFalse(Where + TEXT(": the failing jump does not pass"), PassMet(L.Pass, Bad));
			}
		}
	}
	TestEqual(TEXT("Every fault rule in the catalogue was checked"), RulesChecked, RulesTotal);
	TestTrue(TEXT("The catalogue has fault rules"), RulesTotal > 20);
	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolFaultPriority, "KiteSurf.School.FaultPriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfSchoolFaultPriority::RunTest(const FString& Parameters)
{
	using namespace SchoolCatalogTest;
	const FLessonDef* B3 = LessonCatalog::Find(TEXT("B3"));
	if (!TestNotNull(TEXT("B3 exists"), B3))
	{
		return false;
	}

	// Front stall and dived early together: the front stall (4) beats dived early (3).
	{
		FJumpScene P = LandingDiveScene();
		P.KiteDownwindAtLanding = -2.0f;
		P.DiveToDeg = 40.0f;
		const FSchoolScene S = BuildJump(P);
		const FLessonFault* F = LessonEval::DiagnoseFault(B3->Faults, S.Telemetry, S.Jump, S.Extras);
		TestTrue(TEXT("Front stall over dived early"), F && F->Id == FName(TEXT("FrontStall")));
	}
	// Dived early with the evaluator's KiteTooLow cause: the lesson's own line (3) beats the cause (0).
	{
		FJumpScene P = LandingDiveScene();
		P.DiveToDeg = 40.0f;
		P.Cause = ELandingCause::KiteTooLow;
		P.Grade = ELandingGrade::Sketchy;
		const FSchoolScene S = BuildJump(P);
		const FLessonFault* F = LessonEval::DiagnoseFault(B3->Faults, S.Telemetry, S.Jump, S.Extras);
		TestTrue(TEXT("Dived early over the KiteTooLow cause"), F && F->Id == FName(TEXT("DivedEarly")));
	}
	// The cause alone shows the cause's line.
	{
		FJumpScene P = LandingDiveScene();
		P.Cause = ELandingCause::TooHard;
		const FSchoolScene S = BuildJump(P);
		const FLessonFault* F = LessonEval::DiagnoseFault(B3->Faults, S.Telemetry, S.Jump, S.Extras);
		TestTrue(TEXT("The TooHard cause alone"), F && F->Id == FName(TEXT("CauseTooHard")));
	}

	// Ties go to the earlier rule; unreadable measures never match; an empty list shows nothing.
	const FSchoolScene Jump = BuildJump(FJumpScene());
	TArray<FLessonFault> Rules;
	Rules.Add(LessonRules::Fault(TEXT("First"), LessonRules::Jump(ELessonMetric::JumpHeight), ELessonCompare::Greater, 1.0f, 1, FText::FromString(TEXT("first"))));
	Rules.Add(LessonRules::Fault(TEXT("Second"), LessonRules::Jump(ELessonMetric::JumpHeight), ELessonCompare::Greater, 1.0f, 1, FText::FromString(TEXT("second"))));
	const FLessonFault* Tie = LessonEval::DiagnoseFault(Rules, Jump.Telemetry, Jump.Jump);
	TestTrue(TEXT("A tie goes to the earlier rule"), Tie && Tie->Id == FName(TEXT("First")));
	Rules.Add(LessonRules::Fault(TEXT("Rotation"), LessonRules::Jump(ELessonMetric::RotationDeg), ELessonCompare::Less, 1000.0f, 9, FText::FromString(TEXT("rotation"))));
	const FLessonFault* Unknown = LessonEval::DiagnoseFault(Rules, Jump.Telemetry, Jump.Jump);
	TestTrue(TEXT("An unknown rotation never matches, whatever its priority"), Unknown && Unknown->Id == FName(TEXT("First")));
	Rules.Add(LessonRules::Fault(TEXT("Top"), LessonRules::Jump(ELessonMetric::JumpHeight), ELessonCompare::Greater, 1.0f, 5, FText::FromString(TEXT("top"))));
	const FLessonFault* Top = LessonEval::DiagnoseFault(Rules, Jump.Telemetry, Jump.Jump);
	TestTrue(TEXT("The highest priority wins, wherever it is in the list"), Top && Top->Id == FName(TEXT("Top")));
	TestNull(TEXT("No rules, no line"), LessonEval::DiagnoseFault(TArray<FLessonFault>(), Jump.Telemetry, Jump.Jump));

	// The section 3.4 rules, as named builders, on both sides.
	{
		const FSchoolScene Good = BuildJump(LandingDiveScene());
		FJumpScene Pop = PopScene();
		const FSchoolScene PopGood = BuildJump(Pop);
		Pop.PopRiseDeg = 15.0f;
		const FSchoolScene PopBad = BuildJump(Pop);
		const FText Line = FText::FromString(TEXT("line"));
		TestFalse(TEXT("Kite rose: not on a flat-kite pop"), RuleMatches(LessonRules::KiteRoseDuringPop(0, Line), PopGood));
		TestTrue(TEXT("Kite rose: 15 deg during the pop"), RuleMatches(LessonRules::KiteRoseDuringPop(0, Line), PopBad));
		FJumpScene Low = LandingDiveScene();
		Low.DiveToDeg = 40.0f;
		TestFalse(TEXT("Dived early: kite 60 deg at touchdown"), RuleMatches(LessonRules::DivedEarly(0, Line), Good));
		TestTrue(TEXT("Dived early: kite 40 deg at touchdown"), RuleMatches(LessonRules::DivedEarly(0, Line), BuildJump(Low)));
		FJumpScene Stall = LandingDiveScene();
		Stall.KiteDownwindAtLanding = -1.0f;
		TestFalse(TEXT("Front stall: kite downwind"), RuleMatches(LessonRules::FrontStall(0, Line), Good));
		TestTrue(TEXT("Front stall: kite upwind of the rider"), RuleMatches(LessonRules::FrontStall(0, Line), BuildJump(Stall)));
		FJumpScene Edge;
		Edge.EdgeDropBeforeS = 0.3f;
		TestFalse(TEXT("Edge lost: edge held"), RuleMatches(LessonRules::EdgeLostBeforeTakeoff(0, Line), Jump));
		TestTrue(TEXT("Edge lost: dropped 0.3 s early"), RuleMatches(LessonRules::EdgeLostBeforeTakeoff(0, Line), BuildJump(Edge)));
		FJumpScene Sheeted;
		Sheeted.BarClimbing = 0.9f;
		TestFalse(TEXT("Sheeted in: bar out while climbing"), RuleMatches(LessonRules::SheetedInWhileClimbing(0, Line), Jump));
		TestTrue(TEXT("Sheeted in: bar in while climbing"), RuleMatches(LessonRules::SheetedInWhileClimbing(0, Line), BuildJump(Sheeted)));
		FJumpScene Hard;
		Hard.Cause = ELandingCause::TooHard;
		TestFalse(TEXT("Cause: none"), RuleMatches(LessonRules::LandingCause(ELandingCause::TooHard, 0, Line), Jump));
		TestTrue(TEXT("Cause: TooHard"), RuleMatches(LessonRules::LandingCause(ELandingCause::TooHard, 0, Line), BuildJump(Hard)));
		TestFalse(TEXT("Cause: another cause does not match"), RuleMatches(LessonRules::LandingCause(ELandingCause::Sideways, 0, Line), BuildJump(Hard)));
	}
	return true;
}

#endif
