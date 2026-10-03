#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "BoardMovementComponent.h"
#include "KiteComponent.h"
#include "KiteRiderPawn.h"
#include "KiteSurfHUD.h"
#include "KiteSurfUnits.h"
#include "WindComponent.h"
#include "School/LessonCatalog.h"
#include "School/LessonDirector.h"
#include "School/LessonHUD.h"
#include "School/LessonSubsystem.h"
#include "School/LessonTiming.h"
#include "UI/KiteSurfGameInstance.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the HUD lesson layer (docs/tutorials.md S4): the pure formatters and cue geometry of
// School/LessonHUD.h, the timing grades of School/LessonTiming.h, and AKiteSurfHUD reading a real
// ALessonDirector on a spawned rider (the director fixture of SchoolDirectorTests.cpp).
namespace SchoolHUDTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	const float HUDFrameSeconds = 1.0f / 60.0f;
	constexpr float MSPerKnot = 0.5144f;

	/** A3 as the director would show it on a step. */
	FLessonHUDInput StepInput(int32 StepIndex = 0)
	{
		const FLessonDef* A3 = LessonCatalog::Find(TEXT("A3"));
		check(A3);
		FLessonHUDInput In;
		In.Phase = ELessonPhase::Step;
		In.LessonId = A3->Id;
		In.Title = A3->Title;
		In.Summary = A3->Summary;
		In.StepIndex = StepIndex;
		In.StepCount = A3->Steps.Num();
		In.StepPrompt = A3->Steps[StepIndex].Prompt;
		In.Glyph = A3->Steps[StepIndex].InputGlyph;
		In.Cue = A3->Steps[StepIndex].Cue;
		In.bHasObjective = true;
		In.Objective = StepIndex == A3->Steps.Num() - 1 ? A3->Pass : A3->Steps[StepIndex].Objective;
		In.HigherBarText = A3->Stars.HigherBarText;
		return In;
	}

	/** The director fixture of SchoolDirectorTests.cpp, plus the HUD in the same world. */
	struct FHUDFixture
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		ALessonDirector* Director = nullptr;
		ULessonSubsystem* Lessons = nullptr;
		AKiteSurfHUD* HUD = nullptr;

		FHUDFixture()
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
			HUD = World ? World->SpawnActor<AKiteSurfHUD>() : nullptr;
			Lessons = NewObject<ULessonSubsystem>(NewObject<UKiteSurfGameInstance>());
			Lessons->SetWriteToDisk(false);
			Lessons->SetTravelEnabled(false);
			if (Director)
			{
				Director->SetLessonSubsystem(Lessons);
			}
		}

		~FHUDFixture()
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

		bool IsValid() const { return Pawn && Director && Lessons && HUD; }

		/** One frame: the rider, then the director (its own tick runs after the pawn's), then the HUD. */
		void Frame()
		{
			Pawn->Tick(HUDFrameSeconds);
			Director->UpdateLesson(HUDFrameSeconds);
			HUD->UpdateLessonLayer(HUDFrameSeconds);
		}

		template <typename FDone>
		bool FramesUntil(float Seconds, FDone&& Done)
		{
			for (float T = 0.0f; T < Seconds; T += HUDFrameSeconds)
			{
				Frame();
				if (Done())
				{
					return true;
				}
			}
			return false;
		}

		const FLessonHUDView& View() const { return HUD->GetLessonView(); }
	};

	/** A catalogue lesson cut to one step that passes on its first judged sample (any speed), so a result card comes at once. */
	FLessonDef QuickPassLesson(const TCHAR* Id)
	{
		FLessonDef L = *LessonCatalog::Find(Id);
		FLessonObjective Any;
		Any.Metric = ELessonMetric::Channel;
		Any.Channel = ELessonChannel::Speed;
		Any.Min = 0.0f;
		L.Steps.SetNum(1);
		L.Steps[0].Objective = Any;
		L.Pass = Any;
		L.Stars.HigherBar.Reset();
		return L;
	}
}

using namespace SchoolHUDTest;

// The pure formatters: glyphs, metric values in their units, targets and progress.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfHUDLessonFormatters, "KiteSurf.HUD.LessonFormatters", SchoolHUDTest::Flags)

bool FKiteSurfHUDLessonFormatters::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Sheet glyph"), LessonHUD::GlyphText(TEXT("IA_Sheet")), FString(TEXT("[Up/Down | R stick]")));
	TestEqual(TEXT("Steer glyph"), LessonHUD::GlyphText(TEXT("IA_Steer")), FString(TEXT("[Left/Right | R stick]")));
	TestEqual(TEXT("Jump glyph"), LessonHUD::GlyphText(TEXT("IA_Jump")), FString(TEXT("[Space | A]")));
	TestEqual(TEXT("Reset glyph"), LessonHUD::GlyphText(TEXT("IA_Reset")), FString(TEXT("[R | B]")));
	TestEqual(TEXT("No glyph for None"), LessonHUD::GlyphText(NAME_None), FString());
	for (const FLessonDef& L : LessonCatalog::GetAll())
	{
		for (const FLessonStep& S : L.Steps)
		{
			TestTrue(FString::Printf(TEXT("%s: every step glyph has a text"), *L.Id.ToString()), S.InputGlyph.IsNone() || !LessonHUD::GlyphText(S.InputGlyph).IsEmpty());
		}
	}

	TestEqual(TEXT("Height in m"), LessonHUD::FormatMetricValue(ELessonMetric::JumpHeight, ELessonChannel::Speed, 1.46f), FString(TEXT("1.5 m")));
	TestEqual(TEXT("Speed held in knots"), LessonHUD::FormatMetricValue(ELessonMetric::SpeedHeld, ELessonChannel::Speed, 15.0f * MSPerKnot), FString(TEXT("15.0 kn")));
	TestEqual(TEXT("Elevation in degrees"), LessonHUD::FormatMetricValue(ELessonMetric::KiteElevationHeld, ELessonChannel::Speed, 44.6f), FString(TEXT("45°")));
	TestEqual(TEXT("Grade by name"), LessonHUD::FormatMetricValue(ELessonMetric::LandingGrade, ELessonChannel::Speed, 1.0f), FString(TEXT("Clean")));
	TestEqual(TEXT("A channel metric in its channel's unit"), LessonHUD::FormatMetricValue(ELessonMetric::Channel, ELessonChannel::KiteElevation, 82.0f), FString(TEXT("82°")));
	TestEqual(TEXT("Edge as a share"), LessonHUD::FormatMetricValue(ELessonMetric::ChannelHeld, ELessonChannel::EdgeAbs, 0.7f), FString(TEXT("70%")));

	const FLessonDef* A3 = LessonCatalog::Find(TEXT("A3"));
	const FLessonDef* B2 = LessonCatalog::Find(TEXT("B2"));
	const FLessonDef* A2 = LessonCatalog::Find(TEXT("A2"));
	if (!TestTrue(TEXT("Catalogue lessons"), A3 && B2 && A2))
	{
		return false;
	}
	TestEqual(TEXT("A3 step 1's band"), LessonHUD::FormatTarget(A3->Steps[0].Objective), FString(TEXT("35 - 55°")));
	const FString A3Band = FString::Printf(TEXT("%.1f - %.1f kn"), A3->Pass.Min / MSPerKnot, A3->Pass.Max / MSPerKnot);
	TestEqual(TEXT("A3's speed band in knots (12 kn +-15%, tuned on rides)"), LessonHUD::FormatTarget(A3->Pass), A3Band);
	TestTrue(TEXT("...about 10.2 to 13.8 kn"), FMath::IsNearlyEqual(A3->Pass.Min / MSPerKnot, 10.2f, 0.01f) && FMath::IsNearlyEqual(A3->Pass.Max / MSPerKnot, 13.8f, 0.01f));
	TestEqual(TEXT("B2's jumps"), LessonHUD::FormatTarget(B2->Pass), FString(TEXT("1.0 - 2.0 m")));
	TestEqual(TEXT("B2 step 1: the kite at the top, at least 75 deg"), LessonHUD::FormatTarget(B2->Steps[0].Objective), FString(TEXT("at least 75°")));
	TestEqual(TEXT("A2 step 1: up within 6 s"), LessonHUD::FormatTarget(A2->Steps[0].Objective), FString(TEXT("0.0 - 6.0 s")));

	TestEqual(TEXT("Held progress in seconds"), LessonHUD::FormatProgress(A3->Steps[0].Objective, 0.64f), FString(TEXT("3.2 / 5.0 s")));
	TestEqual(TEXT("Counted progress"), LessonHUD::FormatProgress(B2->Pass, 0.4f), FString(TEXT("2 / 5")));
	TestEqual(TEXT("A single event in percent"), LessonHUD::FormatProgress(B2->Steps[0].Objective, 0.5f), FString(TEXT("50%")));
	TestEqual(TEXT("Progress is clamped"), LessonHUD::FormatProgress(B2->Pass, 1.7f), FString(TEXT("5 / 5")));
	return true;
}

// The view for each phase: nothing when idle, the intro, a step with its prompt, glyph, step counter
// and progress, the fault line after a miss, and the result card.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfHUDLessonPhases, "KiteSurf.HUD.LessonPhases", SchoolHUDTest::Flags)

bool FKiteSurfHUDLessonPhases::RunTest(const FString& Parameters)
{
	const FLessonHUDTimers NoTimers;
	{
		const FLessonHUDView Idle = LessonHUD::BuildView(FLessonHUDInput(), NoTimers);
		TestFalse(TEXT("Idle: nothing shows"), Idle.bVisible || Idle.bPanel || Idle.bResultCard);
		TestEqual(TEXT("Idle: no lines"), Idle.PanelLines().Num(), 0);
	}

	FLessonHUDInput Intro = StepInput(0);
	Intro.Phase = ELessonPhase::Intro;
	Intro.PhaseSeconds = 1.5f;
	Intro.IntroSeconds = 3.0f;
	{
		const FLessonHUDView V = LessonHUD::BuildView(Intro, NoTimers);
		TestTrue(TEXT("Intro: the panel"), V.bVisible && V.bPanel && !V.bResultCard);
		TestEqual(TEXT("Intro header"), V.Header, FString(TEXT("LESSON A3  Speed control")));
		TestEqual(TEXT("Intro prompt is the summary"), V.Prompt, Intro.Summary.ToString());
		TestEqual(TEXT("No glyph in the intro"), V.Glyph, FString());
		TestEqual(TEXT("Get ready"), V.Progress, FString(TEXT("Get ready")));
		TestNearlyEqual(TEXT("The bar counts the intro down"), V.ProgressFraction, 0.5f, 1e-4f);
	}

	FLessonHUDInput Step = StepInput(1);
	Step.Progress = 0.25f;
	Step.Value = 14.0f * MSPerKnot;
	{
		const FLessonHUDView V = LessonHUD::BuildView(Step, NoTimers);
		TestEqual(TEXT("Step header with the counter"), V.Header, FString(TEXT("A3  Speed control   STEP 2/2")));
		TestEqual(TEXT("Step prompt"), V.Prompt, FString(TEXT("Bar in for speed, out to slow down")));
		TestEqual(TEXT("Step glyph"), V.Glyph, FString(TEXT("[Up/Down | R stick]")));
		TestEqual(TEXT("Progress line: held time, value, target"), V.Progress, FString(TEXT("5.0 / 20.0 s   14.0 kn   target ")) + LessonHUD::FormatTarget(Step.Objective));
		TestNearlyEqual(TEXT("Progress fraction"), V.ProgressFraction, 0.25f, 1e-4f);
		TestEqual(TEXT("No fault, hint, timing or drop-back"), V.Fault + V.Hint + V.Timing + V.DropBack, FString());
	}

	FLessonHUDInput Missed = StepInput(0);
	Missed.Phase = ELessonPhase::Result;
	Missed.Outcome = ELessonOutcome::AttemptFailed;
	Missed.FaultLine = FText::FromString(TEXT("Kite too high: fly it at 45°"));
	FLessonHUDTimers FaultUp;
	FaultUp.bShowFault = true;
	{
		const FLessonHUDView V = LessonHUD::BuildView(Missed, FaultUp);
		TestTrue(TEXT("Missed: still the panel, not the card"), V.bPanel && !V.bResultCard);
		TestEqual(TEXT("Missed: the fault line"), V.Fault, FString(TEXT("Kite too high: fly it at 45°")));
		TestEqual(TEXT("Missed: the step's prompt stays"), V.Prompt, FString(TEXT("Fly the kite at 45°")));
		TestEqual(TEXT("Missed: the step counter stays"), V.Header, FString(TEXT("A3  Speed control   STEP 1/2")));
		Missed.FaultLine = FText::GetEmpty();
		TestEqual(TEXT("Missed with no rule matched: a generic line"), LessonHUD::BuildView(Missed, FaultUp).Fault, FString(TEXT("Not quite: try again")));
		TestEqual(TEXT("The fault line is gone once its time is up"), LessonHUD::BuildView(Missed, NoTimers).Fault, FString());
	}

	FLessonHUDInput Offer = StepInput(1);
	Offer.bDropBackOffered = true;
	TestEqual(TEXT("Drop back a step: hold its key, with the fill empty"), LessonHUD::BuildView(Offer, NoTimers).DropBack, FString(TEXT("Too hard? Hold [R | B] to drop back to step 1  [----------]")));
	Offer = StepInput(0);
	Offer.bDropBackOffered = true;
	Offer.DropBackLessonId = TEXT("A2");
	TestEqual(TEXT("On the first step: back to the prerequisite"), LessonHUD::FormatDropBack(Offer), FString(TEXT("Too hard? Hold [R | B] to drop back to A2  [----------]")));
	TestEqual(TEXT("The hold fills the line: 40%"), LessonHUD::FormatDropBack(Offer, 0.4f), FString(TEXT("Too hard? Hold [R | B] to drop back to A2  [####------]")));
	TestEqual(TEXT("Full"), LessonHUD::FormatDropBack(Offer, 1.0f), FString(TEXT("Too hard? Hold [R | B] to drop back to A2  [##########]")));
	Offer.DropBackLessonId = NAME_None;
	TestEqual(TEXT("Nowhere to go: no offer"), LessonHUD::FormatDropBack(Offer), FString());

	FLessonHUDInput Passed = StepInput(1);
	Passed.Phase = ELessonPhase::Result;
	Passed.Outcome = ELessonOutcome::Passed;
	Passed.Stars = 2;
	Passed.Value = 15.0f * MSPerKnot;
	Passed.NextLessonId = TEXT("A4");
	{
		const FLessonHUDView V = LessonHUD::BuildView(Passed, NoTimers);
		TestTrue(TEXT("Passed: the card, not the panel"), V.bResultCard && !V.bPanel && V.bPassed);
		TestEqual(TEXT("Card title"), V.CardTitle, FString(TEXT("LESSON PASSED")));
		TestEqual(TEXT("Card lesson"), V.CardLesson, FString(TEXT("A3  Speed control")));
		TestEqual(TEXT("Card result"), V.CardResult, FString(TEXT("Result  15.0 kn")));
	}
	FLessonHUDInput Failed = Passed;
	Failed.Outcome = ELessonOutcome::Failed;
	Failed.Stars = 0;
	Failed.NextLessonId = NAME_None;
	{
		const FLessonHUDView V = LessonHUD::BuildView(Failed, NoTimers);
		TestTrue(TEXT("Failed: the card"), V.bResultCard && !V.bPassed);
		TestEqual(TEXT("Failed title"), V.CardTitle, FString(TEXT("TIME UP")));
		TestEqual(TEXT("Failed: no stars"), V.CardStars, 0);
		TestEqual(TEXT("Failed: no result value"), V.CardResult, FString());
		TestEqual(TEXT("Failed: Retry and the lesson menu, no Next"), V.CardActions, FString(TEXT("[R | B] Retry    [Esc | Start] Lesson menu")));
	}
	return true;
}

// The result card: filled and empty stars, the pass value, the best from the progress book, what the
// next star asks for, and the Retry / Next / Lesson menu prompts.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfHUDLessonResultCard, "KiteSurf.HUD.LessonResultCard", SchoolHUDTest::Flags)

bool FKiteSurfHUDLessonResultCard::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Stars text"), LessonHUD::FormatStars(2), FString(TEXT("STARS 2/3")));
	TestEqual(TEXT("Stars are clamped"), LessonHUD::FormatStars(7), FString(TEXT("STARS 3/3")));

	const FText Stomped = FText::FromString(TEXT("Stomped landings"));
	TestEqual(TEXT("1 star with assists: the higher bar or fewer assists"), LessonHUD::FormatNextStar(1, false, Stomped, 1), FString(TEXT("Next star: Stomped landings, or fewer assists")));
	TestEqual(TEXT("1 star, no assists to drop: the higher bar"), LessonHUD::FormatNextStar(1, false, Stomped, 0), FString(TEXT("Next star: Stomped landings")));
	TestEqual(TEXT("1 star with the higher bar met: fewer assists"), LessonHUD::FormatNextStar(1, true, Stomped, 1), FString(TEXT("Next star: fewer assists")));
	TestEqual(TEXT("2 stars with assists: every assist off (3 stars = all assists off)"), LessonHUD::FormatNextStar(2, true, Stomped, 1), FString(TEXT("Next star: every assist off")));
	TestEqual(TEXT("3 stars: nothing to ask"), LessonHUD::FormatNextStar(3, true, Stomped, 0), FString());
	TestEqual(TEXT("No pass: nothing to ask"), LessonHUD::FormatNextStar(0, false, Stomped, 1), FString());

	TestEqual(TEXT("After a pass with a next lesson"), LessonHUD::FormatCardActions(true, TEXT("B3")), FString(TEXT("[Space | A] Next: B3    [R | B] Retry    [Esc | Start] Lesson menu")));
	TestEqual(TEXT("After a pass with none"), LessonHUD::FormatCardActions(true, NAME_None), FString(TEXT("[R | B] Retry    [Esc | Start] Lesson menu")));

	const FLessonDef* B2 = LessonCatalog::Find(TEXT("B2"));
	if (!TestNotNull(TEXT("B2"), B2))
	{
		return false;
	}
	FLessonHUDInput In;
	In.Phase = ELessonPhase::Result;
	In.Outcome = ELessonOutcome::Passed;
	In.LessonId = B2->Id;
	In.Title = B2->Title;
	In.bHasObjective = true;
	In.Objective = B2->Pass;
	In.HigherBarText = B2->Stars.HigherBarText;
	In.AssistsOn = 1;
	In.Value = 1.62f;
	In.NextLessonId = TEXT("B3");
	In.bHasRecord = true;
	In.BestStars = 2;
	In.bHasBestValue = true;
	In.BestValue = 1.81f;
	In.BestValueMetric = ELessonMetric::JumpHeight;
	In.Passes = 3;
	for (int32 Stars = 1; Stars <= 3; ++Stars)
	{
		In.Stars = Stars;
		const FLessonHUDView V = LessonHUD::BuildView(In, FLessonHUDTimers());
		TestEqual(FString::Printf(TEXT("%d star(s): that many filled"), Stars), V.CardStars, Stars);
		TestEqual(FString::Printf(TEXT("%d star(s): the text"), Stars), V.CardStarsText, FString::Printf(TEXT("STARS %d/3"), Stars));
	}
	In.Stars = 1;
	In.bPassedHigherBar = false;
	const FLessonHUDView V = LessonHUD::BuildView(In, FLessonHUDTimers());
	const TArray<FString> Expected = {
		TEXT("LESSON PASSED"),
		TEXT("B2  Small jump"),
		TEXT("STARS 1/3"),
		TEXT("Result  1.6 m"),
		TEXT("Best  2/3 stars   1.8 m   3 passes"),
		TEXT("Next star: Stomped landings, or fewer assists"),
		TEXT("[Space | A] Next: B3    [R | B] Retry    [Esc | Start] Lesson menu"),
	};
	TestEqual(TEXT("The card's lines"), FString::Join(V.ResultCardLines(), TEXT("|")), FString::Join(Expected, TEXT("|")));
	In.bPassedHigherBar = true;
	In.Stars = 2;
	TestEqual(TEXT("A higher-bar pass names it by the result"), LessonHUD::BuildView(In, FLessonHUDTimers()).CardResult, FString(TEXT("Result  1.6 m   (Stomped landings)")));
	In.bHasRecord = false;
	TestEqual(TEXT("No record yet"), LessonHUD::BuildView(In, FLessonHUDTimers()).CardBest, FString(TEXT("Best  --")));
	In.Passes = 1;
	In.bHasRecord = true;
	TestTrue(TEXT("One pass, singular"), LessonHUD::BuildView(In, FLessonHUDTimers()).CardBest.EndsWith(TEXT("1 pass")));
	return true;
}

// The cue geometry on the wind-window arc: arc points, target zones per cue, the ghost kite's target
// by phase, the timing ring, and zones fading as stars are earned.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfHUDLessonCueGeometry, "KiteSurf.HUD.LessonCueGeometry", SchoolHUDTest::Flags)

bool FKiteSurfHUDLessonCueGeometry::RunTest(const FString& Parameters)
{
	const FVector2D C(180.0f, 670.0f);
	const float R = 110.0f;
	TestTrue(TEXT("12 o'clock is straight up"), LessonHUD::ArcPoint(C, R, 0.0f).Equals(FVector2D(180.0f, 560.0f), 1e-3f));
	TestTrue(TEXT("+90 is the right horizon"), LessonHUD::ArcPoint(C, R, 90.0f).Equals(FVector2D(290.0f, 670.0f), 1e-3f));
	TestTrue(TEXT("-90 is the left horizon"), LessonHUD::ArcPoint(C, R, -90.0f).Equals(FVector2D(70.0f, 670.0f), 1e-3f));
	TestTrue(TEXT("60 deg deep in the window: half way in"), LessonHUD::ArcPoint(C, R, 0.0f, 60.0f).Equals(FVector2D(180.0f, 615.0f), 1e-3f));
	TestEqual(TEXT("45 deg elevation on the right is clock +45"), LessonHUD::ElevationToClock(45.0f, 1), 45.0f);
	TestEqual(TEXT("80 deg on the left is clock -10"), LessonHUD::ElevationToClock(80.0f, -1), -10.0f);

	const FLessonDef* A3 = LessonCatalog::Find(TEXT("A3"));
	const FLessonDef* B2 = LessonCatalog::Find(TEXT("B2"));
	const FLessonDef* A1 = LessonCatalog::Find(TEXT("A1"));
	const FLessonDef* A7 = LessonCatalog::Find(TEXT("A7"));
	if (!TestTrue(TEXT("Catalogue lessons"), A3 && B2 && A1 && A7))
	{
		return false;
	}

	// A3 step 1 (WindowArc, kite held at 35 to 55 deg): its band either side of 12.
	TArray<FLessonArcZone> Zones = LessonHUD::ArcZones(ELessonCue::WindowArc, &A3->Steps[0].Objective, false, 1);
	if (TestEqual(TEXT("A3 step 1: two target zones"), Zones.Num(), 2))
	{
		TestTrue(TEXT("Left zone -55..-35"), Zones[0].Kind == ELessonArcZoneKind::Target && FMath::IsNearlyEqual(Zones[0].FromClockDeg, -55.0f) && FMath::IsNearlyEqual(Zones[0].ToClockDeg, -35.0f));
		TestTrue(TEXT("Right zone 35..55"), Zones[1].Kind == ELessonArcZoneKind::Target && FMath::IsNearlyEqual(Zones[1].FromClockDeg, 35.0f) && FMath::IsNearlyEqual(Zones[1].ToClockDeg, 55.0f));
	}
	// A band reaching the top (B2 step 1: the kite at the top, 75 deg or more) is one zone across 12.
	Zones = LessonHUD::ArcZones(ELessonCue::WindowArc, &B2->Steps[0].Objective, false, 1);
	TestTrue(TEXT("Over 75 deg: one zone -15..15"), Zones.Num() == 1 && FMath::IsNearlyEqual(Zones[0].FromClockDeg, -15.0f) && FMath::IsNearlyEqual(Zones[0].ToClockDeg, 15.0f));
	// No band: the three zones of docs/tutorials.md 3.4, the sweet spot in front of 12 on the travel side.
	for (const int32 Tack : { 1, -1 })
	{
		Zones = LessonHUD::ArcZones(ELessonCue::WindowArc, nullptr, false, Tack);
		const FLessonArcZone* Landing = Zones.FindByPredicate([](const FLessonArcZone& Z) { return Z.Kind == ELessonArcZoneKind::Landing; });
		const FLessonArcZone* Sheet = Zones.FindByPredicate([](const FLessonArcZone& Z) { return Z.Kind == ELessonArcZoneKind::SheetIn; });
		const int32 Cruise = Zones.FilterByPredicate([](const FLessonArcZone& Z) { return Z.Kind == ELessonArcZoneKind::Cruise; }).Num();
		TestEqual(FString::Printf(TEXT("Tack %+d: four zones"), Tack), Zones.Num(), 4);
		TestEqual(FString::Printf(TEXT("Tack %+d: cruise either side"), Tack), Cruise, 2);
		TestTrue(FString::Printf(TEXT("Tack %+d: sheet-in band -10..10"), Tack), Sheet && FMath::IsNearlyEqual(Sheet->FromClockDeg, -10.0f) && FMath::IsNearlyEqual(Sheet->ToClockDeg, 10.0f));
		TestTrue(FString::Printf(TEXT("Tack %+d: sweet spot 10..25 on the travel side"), Tack),
			Landing && FMath::IsNearlyEqual(Landing->FromClockDeg, Tack > 0 ? 10.0f : -25.0f) && FMath::IsNearlyEqual(Landing->ToClockDeg, Tack > 0 ? 25.0f : -10.0f));
	}
	Zones = LessonHUD::ArcZones(ELessonCue::TimingRing, &B2->Pass, true, 1);
	TestTrue(TEXT("Timing ring on a sheet-in step: the 12 band"), Zones.Num() == 1 && Zones[0].Kind == ELessonArcZoneKind::SheetIn);
	Zones = LessonHUD::ArcZones(ELessonCue::TimingRing, &A7->Steps[0].Objective, false, 1);
	TestTrue(TEXT("Timing ring on a transition: the 12 band"), Zones.Num() == 1 && Zones[0].Kind == ELessonArcZoneKind::SheetIn);
	TestEqual(TEXT("Ghost kite and speed band cues: no zones"), LessonHUD::ArcZones(ELessonCue::GhostKite, &B2->Steps[0].Objective, false, 1).Num()
		+ LessonHUD::ArcZones(ELessonCue::SpeedBand, &A3->Pass, false, 1).Num(), 0);

	// The ghost kite, by phase.
	float Clock = 0.0f;
	float Elevation = 0.0f;
	LessonHUD::GhostTarget(B2->Steps[0].Objective, 45.0f, 45.0f, 1, Clock, Elevation);
	TestTrue(TEXT("B2 send: the ghost waits at 12 (85 deg) on the kite's side"), FMath::IsNearlyEqual(Elevation, 85.0f) && FMath::IsNearlyEqual(Clock, 5.0f));
	LessonHUD::GhostTarget(A1->Steps[0].Objective, 75.0f, -15.0f, 1, Clock, Elevation);
	TestTrue(TEXT("A1 with the kite high: dive to 45 deg on its side"), FMath::IsNearlyEqual(Elevation, 45.0f) && FMath::IsNearlyEqual(Clock, -45.0f));
	LessonHUD::GhostTarget(A1->Steps[0].Objective, 40.0f, -50.0f, 1, Clock, Elevation);
	TestTrue(TEXT("A1 with the kite low: back up to 80 deg"), FMath::IsNearlyEqual(Elevation, 80.0f) && FMath::IsNearlyEqual(Clock, -10.0f));
	LessonHUD::GhostTarget(A3->Steps[0].Objective, 70.0f, 20.0f, 1, Clock, Elevation);
	TestTrue(TEXT("An elevation band: its middle"), FMath::IsNearlyEqual(Elevation, 45.0f) && FMath::IsNearlyEqual(Clock, 45.0f));

	TestNearlyEqual(TEXT("Timing ring at 12: the marker's size"), LessonHUD::TimingRingRadius(0.0f, 9.0f), 9.0f, 1e-4f);
	TestNearlyEqual(TEXT("Timing ring 60 deg off: four times"), LessonHUD::TimingRingRadius(-60.0f, 9.0f), 36.0f, 1e-4f);
	TestTrue(TEXT("The ring closes as the kite nears 12"), LessonHUD::TimingRingRadius(20.0f, 9.0f) < LessonHUD::TimingRingRadius(40.0f, 9.0f));
	TestNearlyEqual(TEXT("Zones at full strength with no stars"), LessonHUD::ZoneAlpha(0), 1.0f, 1e-4f);
	TestNearlyEqual(TEXT("Zones faded at three stars"), LessonHUD::ZoneAlpha(3), 0.4f, 1e-4f);
	TestTrue(TEXT("Zones fade with each star"), LessonHUD::ZoneAlpha(2) < LessonHUD::ZoneAlpha(1));
	return true;
}

// The hint for a held objective going wrong: after 2 s out of band, the lesson's matching fault line
// (the faults S3 never showed for held objectives), or a line from the objective.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfHUDLessonHintTiming, "KiteSurf.HUD.LessonHintTiming", SchoolHUDTest::Flags)

bool FKiteSurfHUDLessonHintTiming::RunTest(const FString& Parameters)
{
	FLessonHintTimer Timer;
	bool bShown = false;
	for (int32 I = 0; I < 114; ++I) // 1.9 s
	{
		bShown |= Timer.Update(true, false, HUDFrameSeconds);
	}
	TestFalse(TEXT("Not before 2 s out of band"), bShown);
	for (int32 I = 0; I < 12; ++I) // 2.1 s
	{
		bShown = Timer.Update(true, false, HUDFrameSeconds);
	}
	TestTrue(TEXT("After 2 s out of band"), bShown);
	TestFalse(TEXT("Back in band: gone at once"), Timer.Update(true, true, HUDFrameSeconds));
	TestFalse(TEXT("...and the count starts again"), Timer.Update(true, false, 1.5f));
	TestTrue(TEXT("...reaching 2 s again"), Timer.Update(true, false, 0.6f));
	TestFalse(TEXT("Not a held objective: never"), Timer.Update(false, false, 5.0f));

	const FLessonDef* A3 = LessonCatalog::Find(TEXT("A3"));
	if (!TestNotNull(TEXT("A3"), A3))
	{
		return false;
	}
	// A3 step 1 with the kite parked at 70 deg: the lesson's own line for it.
	FLessonTelemetry High;
	FLessonTelemetry Low;
	for (int32 I = 0; I <= 300; ++I)
	{
		FLessonSample S;
		S.TimeSeconds = I / 60.0f;
		S.BoardState = EBoardState::Planing;
		S.SpeedMS = 5.0f;
		S.KiteElevationDeg = 70.0f;
		High.Add(S);
		S.KiteElevationDeg = 45.0f;
		Low.Add(S);
	}
	TestEqual(TEXT("Kite parked high: the lesson's fault line"), LessonHUD::HeldHintLine(A3->Faults, A3->Steps[0].Objective, High, 70.0f), FString(TEXT("Kite too high: fly it at 45°")));
	TestEqual(TEXT("Too slow on A3 step 2 (kite fine): a line from the objective"), LessonHUD::HeldHintLine(A3->Faults, A3->Pass, Low, 5.0f), FString(TEXT("Too slow: bar in")));
	TestEqual(TEXT("Too fast"), LessonHUD::HeldHintLine(A3->Faults, A3->Pass, Low, 10.0f), FString(TEXT("Too fast: bar out")));
	TestEqual(TEXT("Kite too low"), LessonHUD::HeldHintLine(A3->Faults, A3->Steps[0].Objective, Low, 20.0f), FString(TEXT("Kite too low: steer it up")));

	// Through the layer: the hint comes after 2 s out of band in a step, and never over the fault line.
	FLessonHUDLayer Layer;
	FLessonHUDInput In = StepInput(0);
	In.bInBand = false;
	In.HeldHint = TEXT("Kite too high: fly it at 45°");
	for (int32 I = 0; I < 60; ++I)
	{
		Layer.UpdateFromInput(In, HUDFrameSeconds);
	}
	TestEqual(TEXT("Layer: no hint after 1 s"), Layer.GetView().Hint, FString());
	for (int32 I = 0; I < 70; ++I)
	{
		Layer.UpdateFromInput(In, HUDFrameSeconds);
	}
	TestEqual(TEXT("Layer: the hint after 2 s"), Layer.GetView().Hint, In.HeldHint);
	In.bInBand = true;
	Layer.UpdateFromInput(In, HUDFrameSeconds);
	TestEqual(TEXT("Layer: back in band, no hint"), Layer.GetView().Hint, FString());
	return true;
}

// Timing grades: the pure graders, the live sheet-in grade from the telemetry, and the layer flashing
// each new grade once for LessonHUD::TimingFlashSeconds.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfHUDLessonTimingGrades, "KiteSurf.HUD.LessonTimingGrades", SchoolHUDTest::Flags)

bool FKiteSurfHUDLessonTimingGrades::RunTest(const FString& Parameters)
{
	using G = ELessonTimingGrade;
	TestEqual(TEXT("Sheet in with the kite at 50 deg: early"), LessonTiming::GradeSheetIn(50.0f, 0.0f), G::Early);
	TestEqual(TEXT("At 70 deg, still climbing: good"), LessonTiming::GradeSheetIn(70.0f, 0.0f), G::Good);
	TestEqual(TEXT("At 72 deg once it has stopped climbing: the top, perfect"), LessonTiming::GradeSheetIn(72.0f, 0.0f, 0.0f), G::Perfect);
	TestEqual(TEXT("At 85 deg just as it got there: perfect"), LessonTiming::GradeSheetIn(85.0f, 0.1f), G::Perfect);
	TestEqual(TEXT("At 85 deg after half a second there: good"), LessonTiming::GradeSheetIn(85.0f, 0.5f), G::Good);
	TestEqual(TEXT("At 85 deg after a second there: late"), LessonTiming::GradeSheetIn(85.0f, 1.0f), G::Late);
	TestEqual(TEXT("Dive lead in the middle of 0.15..1 s: perfect"), LessonTiming::GradeInBand(0.55f, 0.15f, 1.0f, true), G::Perfect);
	TestEqual(TEXT("Dive lead near the edge: good"), LessonTiming::GradeInBand(0.2f, 0.15f, 1.0f, true), G::Good);
	TestEqual(TEXT("Dived 1.4 s before touchdown: early"), LessonTiming::GradeInBand(1.4f, 0.15f, 1.0f, true), G::Early);
	TestEqual(TEXT("Dived 0.05 s before: late"), LessonTiming::GradeInBand(0.05f, 0.15f, 1.0f, true), G::Late);
	TestEqual(TEXT("Offset 0.05 s: perfect"), LessonTiming::GradeOffset(0.05f, 0.1f, 0.3f), G::Perfect);
	TestEqual(TEXT("Offset -0.5 s: early"), LessonTiming::GradeOffset(-0.5f, 0.1f, 0.3f), G::Early);
	TestEqual(TEXT("Offset +0.2 s: good"), LessonTiming::GradeOffset(0.2f, 0.1f, 0.3f), G::Good);
	TestEqual(TEXT("Offset +0.5 s: late"), LessonTiming::GradeOffset(0.5f, 0.1f, 0.3f), G::Late);
	TestEqual(TEXT("Grade text"), LessonTiming::GradeText(G::Perfect), FString(TEXT("PERFECT")));

	// Live sheet-in: the kite climbs to 85 deg; the bar comes in at a chosen moment.
	auto Ride = [](float SheetAt, float Until)
	{
		FLessonTelemetry T;
		G Grade = G::None;
		int32 Grades = 0;
		for (int32 I = 0; I * (1.0f / 60.0f) <= Until; ++I)
		{
			const float Time = I / 60.0f;
			FLessonSample S;
			S.TimeSeconds = Time;
			S.BoardState = EBoardState::Planing;
			S.KiteElevationDeg = FMath::Lerp(45.0f, 85.0f, FMath::Clamp(Time / 2.0f, 0.0f, 1.0f)); // 75 deg (the top) at 1.5 s
			S.BarPosition = Time >= SheetAt ? 0.9f : 0.5f;
			T.Add(S);
			G Now = G::None;
			if (LessonTiming::DetectSheetIn(T, Now))
			{
				Grade = Now;
				++Grades;
			}
		}
		return TPair<G, int32>(Grade, Grades);
	};
	TestEqual(TEXT("Bar in at 55 deg while it climbs: early"), Ride(0.5f, 3.0f).Key, G::Early);
	TestEqual(TEXT("Bar in at 70 deg while it climbs: good"), Ride(1.25f, 3.0f).Key, G::Good);
	TestEqual(TEXT("Bar in as the kite reaches the top (76 deg): perfect"), Ride(1.55f, 3.0f).Key, G::Perfect);
	TestEqual(TEXT("Bar in a second after it got there: late"), Ride(2.8f, 3.5f).Key, G::Late);
	TestEqual(TEXT("One pull is one grade"), Ride(1.55f, 3.0f).Value, 1);
	TestEqual(TEXT("Never pulled: no grade"), Ride(10.0f, 3.0f).Value, 0);

	// The layer flashes a grade when its serial moves, for the flash time, and not again.
	FLessonHUDLayer Layer;
	FLessonHUDInput In = StepInput(0);
	In.TimingSerial = 3; // a run already graded three times when the HUD first looks
	In.TimingGrade = G::Good;
	Layer.UpdateFromInput(In, HUDFrameSeconds);
	TestEqual(TEXT("A grade from before the HUD looked is not flashed"), Layer.GetView().Timing, FString());
	In.TimingSerial = 4;
	In.TimingGrade = G::Perfect;
	Layer.UpdateFromInput(In, HUDFrameSeconds);
	TestEqual(TEXT("A new grade flashes"), Layer.GetView().Timing, FString(TEXT("PERFECT")));
	TestEqual(TEXT("...with its grade for the colour"), Layer.GetView().TimingGrade, G::Perfect);
	for (int32 I = 0; I < 60; ++I)
	{
		Layer.UpdateFromInput(In, HUDFrameSeconds);
	}
	TestEqual(TEXT("Still up after 1 s"), Layer.GetView().Timing, FString(TEXT("PERFECT")));
	for (int32 I = 0; I < 20; ++I)
	{
		Layer.UpdateFromInput(In, HUDFrameSeconds);
	}
	TestEqual(TEXT("Gone after 1.2 s"), Layer.GetView().Timing, FString());
	return true;
}

// AKiteSurfHUD on a real director (A2 on a spawned rider): the intro, the step with its prompt, glyph
// and counter, a missed attempt's fault line for 2.5 s, the drop-back offer, and its key held for 1 s
// taking it (a tap only resets the rider).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfHUDLessonDirectorRun, "KiteSurf.HUD.LessonDirectorRun", SchoolHUDTest::Flags)

bool FKiteSurfHUDLessonDirectorRun::RunTest(const FString& Parameters)
{
	FHUDFixture Fx;
	const FLessonDef* A2 = LessonCatalog::Find(TEXT("A2"));
	if (!TestTrue(TEXT("Fixture and lesson A2"), Fx.IsValid() && A2))
	{
		return false;
	}
	Fx.HUD->UpdateLessonLayer(HUDFrameSeconds);
	TestFalse(TEXT("No lesson: the layer is hidden"), Fx.HUD->IsLessonLayerVisible());
	TestFalse(TEXT("No lesson: the result-card keys do nothing"), Fx.HUD->HandleLessonAction(ELessonHUDAction::Retry));

	Fx.Director->IntroSeconds = 1.0f;
	// The offer after the first miss rather than the third: A2's first step (planing within 6 s) is
	// missed once and then waits for the rider, so three misses would take a scripted water start.
	Fx.Director->DropBackAfterFailures = 1;
	TestTrue(TEXT("A2 begins"), Fx.Director->BeginLesson(*A2, Fx.Pawn));
	Fx.Frame();
	TestEqual(TEXT("The HUD finds the director"), Fx.HUD->FindLessonDirector(), Fx.Director);
	TestTrue(TEXT("Intro: the panel is up"), Fx.HUD->IsLessonLayerVisible() && Fx.View().bPanel);
	TestEqual(TEXT("Intro header"), Fx.View().Header, FString(TEXT("LESSON A2  Water start")));
	TestEqual(TEXT("Intro text is the summary"), Fx.View().Prompt, A2->Summary.ToString());

	const bool bStep = Fx.FramesUntil(3.0f, [&] { return Fx.Director->GetPhase() == ELessonPhase::Step; });
	TestTrue(TEXT("The first step comes after the intro"), bStep);
	TestEqual(TEXT("Step header with its counter"), Fx.View().Header, FString(TEXT("A2  Water start   STEP 1/2")));
	TestEqual(TEXT("Step prompt"), Fx.View().Prompt, FString(TEXT("Dive the kite to get up")));
	TestEqual(TEXT("Step glyph"), Fx.View().Glyph, FString(TEXT("[Left/Right | R stick]")));
	TestTrue(TEXT("Step progress line with its target"), Fx.View().Progress.Contains(TEXT("target 0.0 - 6.0 s")));

	// Left floating with the kite at 12, the rider never gets up within 6 s: a missed attempt.
	const bool bMissed = Fx.FramesUntil(20.0f, [&] { return Fx.Director->GetOutcome() == ELessonOutcome::AttemptFailed; });
	if (!TestTrue(TEXT("Floating 6 s without planing misses the step"), bMissed))
	{
		return false;
	}
	const FString Expected = Fx.Director->GetLastFaultLine().IsEmpty() ? FString(TEXT("Not quite: try again")) : Fx.Director->GetLastFaultLine().ToString();
	TestEqual(TEXT("The fault line shows"), Fx.View().Fault, Expected);
	TestEqual(TEXT("The step's prompt stays above it"), Fx.View().Prompt, FString(TEXT("Dive the kite to get up")));
	TestFalse(TEXT("A missed attempt is not the result card"), Fx.View().bResultCard);
	TestFalse(TEXT("The jump key does nothing on a missed attempt"), Fx.HUD->HandleLessonAction(ELessonHUDAction::Confirm));
	for (int32 I = 0; I < 120; ++I)
	{
		Fx.Frame();
	}
	TestEqual(TEXT("Still up after 2 s"), Fx.View().Fault, Expected);
	const bool bCleared = Fx.FramesUntil(1.0f, [&] { return Fx.View().Fault.IsEmpty(); });
	TestTrue(TEXT("Gone by 3 s"), bCleared);

	// The offer on the first step: drop back to A1, with its key.
	if (!TestTrue(TEXT("The miss raised the drop-back offer"), Fx.Director->IsDropBackOffered()))
	{
		return false;
	}
	Fx.HUD->UpdateLessonLayer(HUDFrameSeconds);
	TestEqual(TEXT("The offer: hold its key"), Fx.View().DropBack, FString(TEXT("Too hard? Hold [R | B] to drop back to A1  [----------]")));
	// A tap: the reset key's press is not the HUD's (the rider's own handler resets the rider) and a
	// short hold takes nothing.
	TestFalse(TEXT("A press of the reset key does not take the offer"), Fx.HUD->HandleLessonAction(ELessonHUDAction::Retry));
	Fx.HUD->SetLessonResetHeld(true);
	for (int32 I = 0; I < 12; ++I)
	{
		Fx.Frame();
	}
	Fx.HUD->SetLessonResetHeld(false);
	Fx.Frame();
	TestEqual(TEXT("After a 0.2 s tap: still A2"), Fx.Director->GetLessonId(), FName(TEXT("A2")));
	TestTrue(TEXT("...with the offer still up"), Fx.Director->IsDropBackOffered());
	TestEqual(TEXT("...and the fill empty again"), Fx.View().DropBack, FString(TEXT("Too hard? Hold [R | B] to drop back to A1  [----------]")));
	// A hold: the fill grows with it, and at 1 s the offer is taken.
	Fx.HUD->SetLessonResetHeld(true);
	for (int32 I = 0; I < 30; ++I)
	{
		Fx.Frame();
	}
	TestEqual(TEXT("Half a second into the hold: half full"), Fx.View().DropBack, FString(TEXT("Too hard? Hold [R | B] to drop back to A1  [#####-----]")));
	TestTrue(TEXT("...and the bar behind it too"), FMath::IsNearlyEqual(Fx.View().DropBackFill, 0.5f, 0.02f));
	TestEqual(TEXT("Not taken yet"), Fx.Director->GetLessonId(), FName(TEXT("A2")));
	const bool bDropped = Fx.FramesUntil(0.6f, [&] { return Fx.Director->GetLessonId() == FName(TEXT("A1")); });
	TestTrue(TEXT("Held for 1 s: dropped back to A1"), bDropped);
	Fx.HUD->SetLessonResetHeld(false);
	TestEqual(TEXT("A1 runs"), Fx.Director->GetLessonId(), FName(TEXT("A1")));
	Fx.HUD->UpdateLessonLayer(HUDFrameSeconds);
	TestEqual(TEXT("The HUD shows A1's intro"), Fx.View().Header, FString(TEXT("LESSON A1  Kite power dive")));
	TestEqual(TEXT("No drop-back line any more"), Fx.View().DropBack, FString());

	Fx.Director->ExitToFreeRide();
	Fx.HUD->UpdateLessonLayer(HUDFrameSeconds);
	TestFalse(TEXT("After exit the layer is hidden"), Fx.HUD->IsLessonLayerVisible());
	return true;
}

// The result card on a real director: a pass shows the stars and the best from the progress book,
// the jump key goes to the next lesson; a time-out shows TIME UP and the reset key retries; the pause
// key asks the lesson menu hook, which answers only once something is bound to it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfHUDLessonDirectorResult, "KiteSurf.HUD.LessonDirectorResult", SchoolHUDTest::Flags)

bool FKiteSurfHUDLessonDirectorResult::RunTest(const FString& Parameters)
{
	FHUDFixture Fx;
	if (!TestTrue(TEXT("Fixture"), Fx.IsValid()))
	{
		return false;
	}
	Fx.Director->IntroSeconds = 0.0f;
	const FLessonDef Quick = QuickPassLesson(TEXT("A3"));
	TestTrue(TEXT("A quick A3 begins"), Fx.Director->BeginLesson(Quick, Fx.Pawn));
	const bool bPassed = Fx.FramesUntil(3.0f, [&] { return Fx.Director->GetOutcome() == ELessonOutcome::Passed; });
	if (!TestTrue(TEXT("It passes at once"), bPassed))
	{
		return false;
	}
	const FLessonHUDView& V = Fx.View();
	TestTrue(TEXT("The result card, not the panel"), V.bResultCard && !V.bPanel && V.bPassed);
	TestEqual(TEXT("Title"), V.CardTitle, FString(TEXT("LESSON PASSED")));
	TestEqual(TEXT("The director's stars, filled"), V.CardStars, Fx.Director->GetStars());
	TestTrue(TEXT("At least one star for a pass"), V.CardStars >= 1);
	TestEqual(TEXT("Stars text"), V.CardStarsText, FString::Printf(TEXT("STARS %d/3"), Fx.Director->GetStars()));
	TestTrue(TEXT("The pass value"), V.CardResult.StartsWith(TEXT("Result  ")) && V.CardResult.Contains(TEXT("kn")));
	TestTrue(TEXT("The best from the progress book: one pass"), V.CardBest.StartsWith(FString::Printf(TEXT("Best  %d/3 stars"), Fx.Director->GetStars())) && V.CardBest.EndsWith(TEXT("1 pass")));
	TestEqual(TEXT("Next, Retry and Lesson menu"), V.CardActions, FString(TEXT("[Space | A] Next: A4    [R | B] Retry    [Esc | Start] Lesson menu")));

	// The pause key: nothing bound, so the HUD falls back to the pause menu.
	TestFalse(TEXT("No lesson menu bound: not handled"), Fx.HUD->HandleLessonAction(ELessonHUDAction::Menu));
	int32 MenuRequests = 0;
	const FDelegateHandle Handle = Fx.Lessons->OnLessonMenuRequested.AddLambda([&MenuRequests] { ++MenuRequests; });
	TestTrue(TEXT("Bound (S5): the lesson menu is asked"), Fx.HUD->HandleLessonAction(ELessonHUDAction::Menu));
	TestEqual(TEXT("Asked once"), MenuRequests, 1);
	Fx.Lessons->OnLessonMenuRequested.Remove(Handle);

	// The jump key: Next.
	TestTrue(TEXT("The jump key goes to the next lesson"), Fx.HUD->HandleLessonAction(ELessonHUDAction::Confirm));
	TestEqual(TEXT("A4 runs"), Fx.Director->GetLessonId(), FName(TEXT("A4")));
	Fx.Frame();
	TestFalse(TEXT("The card is gone"), Fx.View().bResultCard);

	// A time-out on A4: TIME UP, then the reset key retries.
	Fx.Director->LessonTimeLimitSeconds = 0.5f;
	const bool bFailed = Fx.FramesUntil(5.0f, [&] { return Fx.Director->GetOutcome() == ELessonOutcome::Failed; });
	if (!TestTrue(TEXT("A4 times out"), bFailed))
	{
		return false;
	}
	TestTrue(TEXT("The card after a time-out"), Fx.View().bResultCard && !Fx.View().bPassed);
	TestEqual(TEXT("TIME UP"), Fx.View().CardTitle, FString(TEXT("TIME UP")));
	TestEqual(TEXT("No stars"), Fx.View().CardStars, 0);
	TestEqual(TEXT("Retry and Lesson menu only"), Fx.View().CardActions, FString(TEXT("[R | B] Retry    [Esc | Start] Lesson menu")));
	TestFalse(TEXT("The jump key does nothing after a time-out"), Fx.HUD->HandleLessonAction(ELessonHUDAction::Confirm));
	TestTrue(TEXT("The reset key retries"), Fx.HUD->HandleLessonAction(ELessonHUDAction::Retry));
	TestEqual(TEXT("A4 again"), Fx.Director->GetLessonId(), FName(TEXT("A4")));
	TestNotEqual(TEXT("Not on the result any more"), Fx.Director->GetPhase(), ELessonPhase::Result);
	Fx.Director->ExitToFreeRide();
	return true;
}

#endif
