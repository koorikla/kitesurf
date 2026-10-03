#include "School/LessonHUD.h"

#include "GameFramework/HUD.h"
#include "KiteComponent.h"
#include "KiteRiderPawn.h"
#include "KiteSurfUnits.h"
#include "School/LessonProgress.h"
#include "School/LessonSubsystem.h"
#include "Tricks/JumpRecord.h"

// Named, not anonymous, so a unity build cannot merge these helpers with another file's.
namespace LessonHUDPrivate
{
	constexpr float MSPerKnot = 0.5144f;

	bool HasMin(float Min) { return Min > -UE_BIG_NUMBER * 0.5f; }
	bool HasMax(float Max) { return Max < UE_BIG_NUMBER * 0.5f; }

	/** A measure that needs a jump record (as the director's own rule). */
	bool NeedsJump(const FLessonMeasure& Measure)
	{
		if (LessonEval::GetMetricSource(Measure.Metric) == ELessonMetricSource::Jump)
		{
			return true;
		}
		return Measure.Metric == ELessonMetric::Channel
			&& (Measure.Anchor == ELessonAnchor::Takeoff || Measure.Anchor == ELessonAnchor::Apex || Measure.Anchor == ELessonAnchor::Touchdown);
	}

	FString ChannelValue(ELessonChannel Channel, float Value)
	{
		switch (Channel)
		{
		case ELessonChannel::Speed:
			return FString::Printf(TEXT("%.1f kn"), Value / MSPerKnot);
		case ELessonChannel::Heading:
		case ELessonChannel::KiteClock:
		case ELessonChannel::KiteClockAbs:
		case ELessonChannel::KiteElevation:
		case ELessonChannel::KiteBearing:
			return FString::Printf(TEXT("%.0f°"), Value);
		case ELessonChannel::KiteClimbRate:
			return FString::Printf(TEXT("%.0f°/s"), Value);
		case ELessonChannel::Edge:
		case ELessonChannel::EdgeAbs:
		case ELessonChannel::Bar:
		case ELessonChannel::Steer:
		case ELessonChannel::BarWhileClimbing:
			return FString::Printf(TEXT("%.0f%%"), Value * 100.0f);
		case ELessonChannel::Height:
		case ELessonChannel::KiteDownwind:
		case ELessonChannel::Upwind:
			return FString::Printf(TEXT("%.1f m"), Value);
		case ELessonChannel::VerticalSpeed:
			return FString::Printf(TEXT("%.1f m/s"), Value);
		case ELessonChannel::Tension:
			return FString::Printf(TEXT("%.0f N"), Value);
		default:
			return FString::Printf(TEXT("%.0f"), Value);
		}
	}

	FString GradeName(float Index)
	{
		switch (FMath::RoundToInt(Index))
		{
		case 0:  return TEXT("Stomped");
		case 1:  return TEXT("Clean");
		case 2:  return TEXT("Sketchy");
		default: return TEXT("Crash");
		}
	}

	FLinearColor TimingColor(ELessonTimingGrade Grade, float Alpha)
	{
		switch (Grade)
		{
		case ELessonTimingGrade::Perfect: return FLinearColor(1.0f, 0.85f, 0.2f, Alpha);
		case ELessonTimingGrade::Good:    return FLinearColor(0.4f, 1.0f, 0.5f, Alpha);
		default:                          return FLinearColor(1.0f, 0.6f, 0.2f, Alpha);
		}
	}

	FLinearColor ZoneColor(ELessonArcZoneKind Kind, float Alpha)
	{
		switch (Kind)
		{
		case ELessonArcZoneKind::SheetIn: return FLinearColor(1.0f, 0.85f, 0.2f, 0.9f * Alpha);
		case ELessonArcZoneKind::Landing: return FLinearColor(0.3f, 0.9f, 1.0f, 0.9f * Alpha);
		default:                          return FLinearColor(0.35f, 1.0f, 0.45f, 0.9f * Alpha);
		}
	}

	/** The UI scale of the session panel: from the screen height, inside limits. */
	float UiScale(float ScreenH) { return FMath::Clamp(ScreenH / 1080.0f, 0.85f, 1.6f); }

	/** A five-pointed star: filled by fanning lines from the centre, or an outline. */
	void DrawStar(AHUD& HUD, const FVector2D& Centre, float Radius, bool bFilled, const FLinearColor& Color)
	{
		FVector2D Points[10];
		for (int32 I = 0; I < 10; ++I)
		{
			const float R = (I % 2 == 0) ? Radius : Radius * 0.45f;
			const float A = FMath::DegreesToRadians(-90.0f + 36.0f * I);
			Points[I] = Centre + FVector2D(R * FMath::Cos(A), R * FMath::Sin(A));
		}
		for (int32 I = 0; I < 10; ++I)
		{
			const FVector2D& P0 = Points[I];
			const FVector2D& P1 = Points[(I + 1) % 10];
			if (bFilled)
			{
				for (float T = 0.0f; T <= 1.0f; T += 0.1f)
				{
					const FVector2D P = FMath::Lerp(P0, P1, T);
					HUD.DrawLine(Centre.X, Centre.Y, P.X, P.Y, Color, 2.5f);
				}
			}
			HUD.DrawLine(P0.X, P0.Y, P1.X, P1.Y, Color, 2.0f);
		}
	}

	/** A thick stretch of arc just outside the window's edge. */
	void DrawArcBand(AHUD& HUD, const FVector2D& Centre, float Radius, float FromClockDeg, float ToClockDeg, const FLinearColor& Color, float Thickness)
	{
		const int32 Segments = FMath::Max(2, FMath::CeilToInt((ToClockDeg - FromClockDeg) / 4.0f));
		for (int32 I = 0; I < Segments; ++I)
		{
			const float C0 = FMath::Lerp(FromClockDeg, ToClockDeg, float(I) / Segments);
			const float C1 = FMath::Lerp(FromClockDeg, ToClockDeg, float(I + 1) / Segments);
			const FVector2D P0 = LessonHUD::ArcPoint(Centre, Radius, C0);
			const FVector2D P1 = LessonHUD::ArcPoint(Centre, Radius, C1);
			HUD.DrawLine(P0.X, P0.Y, P1.X, P1.Y, Color, Thickness);
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------

FLessonHUDInput FLessonHUDInput::FromDirector(const ALessonDirector& Director)
{
	FLessonHUDInput In;
	const FLessonDef* Lesson = Director.GetLesson();
	In.Phase = Director.GetPhase();
	if (!Lesson || In.Phase == ELessonPhase::Idle)
	{
		In.Phase = ELessonPhase::Idle;
		return In;
	}
	In.Outcome = Director.GetOutcome();
	In.LessonId = Lesson->Id;
	In.Title = Lesson->Title;
	In.Summary = Lesson->Summary;
	In.StepIndex = Director.GetStepIndex();
	In.StepCount = Director.GetStepCount();
	if (Lesson->Steps.IsValidIndex(In.StepIndex))
	{
		const FLessonStep& Step = Lesson->Steps[In.StepIndex];
		In.StepPrompt = Step.Prompt;
		In.bSheetInStep = LessonTiming::IsSheetInStep(Step);
	}
	In.Glyph = Director.GetInputGlyph();
	In.Cue = Director.GetCue();
	if (const FLessonObjective* Objective = Director.GetCurrentObjective())
	{
		In.bHasObjective = true;
		In.Objective = *Objective;
	}
	In.Progress = Director.GetObjectiveProgress();
	In.Value = Director.GetObjectiveValue();
	In.bInBand = Director.IsObjectiveInBand();
	In.FaultLine = Director.GetLastFaultLine();
	In.Stars = Director.GetStars();
	In.bPassedHigherBar = Director.PassedHigherBar();
	In.HigherBarText = Lesson->Stars.HigherBarText;
	In.AssistsOn = Director.GetUsedAssists().CountOn();
	In.bDropBackOffered = Director.IsDropBackOffered();
	In.DropBackLessonId = Director.GetDropBackLessonId();
	In.NextLessonId = In.IsResultCardUp() && In.Outcome == ELessonOutcome::Passed ? Director.GetNextLessonId() : NAME_None;
	In.PhaseSeconds = Director.GetPhaseSeconds();
	In.IntroSeconds = Director.IntroSeconds;
	In.TimingGrade = Director.GetTimingGrade();
	In.TimingSerial = Director.GetTimingSerial();

	if (const ULessonSubsystem* Lessons = Director.GetLessonSubsystem())
	{
		if (const FLessonRecord* Record = Lessons->GetProgress().Find(Lesson->Id))
		{
			In.bHasRecord = true;
			In.BestStars = Record->BestStars;
			In.bHasBestValue = Record->bHasBestValue;
			In.BestValue = Record->BestValue;
			In.BestValueMetric = Record->BestValueMetric;
			In.Passes = Record->Passes;
		}
	}

	const FLessonTelemetry& Telemetry = Director.GetTelemetry();
	if (!Telemetry.IsEmpty())
	{
		const FLessonSample& Latest = Telemetry.Latest();
		In.Tack = Latest.Tack != 0 ? Latest.Tack : 1;
		In.SpeedMS = Latest.SpeedMS;
	}
	if (const AKiteRiderPawn* Rider = Director.GetRider())
	{
		if (const UKiteComponent* Kite = Rider->GetKite())
		{
			In.KiteClockDeg = Kite->GetClockDeg();
			In.KiteDepthDeg = Kite->GetWindowDepthDeg();
			In.KiteElevationDeg = Kite->GetElevationDeg();
		}
	}
	if (In.bHasObjective && LessonEval::GetMetricSource(In.Objective.Metric) == ELessonMetricSource::Held && !In.bInBand)
	{
		In.HeldHint = LessonHUD::HeldHintLine(Lesson->Faults, In.Objective, Telemetry, In.Value);
	}
	return In;
}

// ---------------------------------------------------------------------------------------------
// View
// ---------------------------------------------------------------------------------------------

TArray<FString> FLessonHUDView::ResultCardLines() const
{
	TArray<FString> Lines;
	for (const FString* Line : { &CardTitle, &CardLesson, &CardStarsText, &CardResult, &CardBest, &CardNextStar, &CardActions })
	{
		if (!Line->IsEmpty())
		{
			Lines.Add(*Line);
		}
	}
	return Lines;
}

TArray<FString> FLessonHUDView::PanelLines() const
{
	TArray<FString> Lines;
	for (const FString* Line : { &Header, &Prompt, &Glyph, &Fault, &Progress, &Hint, &DropBack, &Timing })
	{
		if (!Line->IsEmpty())
		{
			Lines.Add(*Line);
		}
	}
	return Lines;
}

bool FLessonHintTimer::Update(bool bHeldObjective, bool bInBand, float DeltaSeconds)
{
	if (!bHeldObjective || bInBand)
	{
		OutOfBandSeconds = 0.0f;
		return false;
	}
	OutOfBandSeconds += FMath::Max(DeltaSeconds, 0.0f);
	return OutOfBandSeconds > LessonHUD::HintAfterSeconds;
}

FString LessonHUD::GlyphText(FName InputAction)
{
	static const TMap<FName, FString> Glyphs = {
		{ TEXT("IA_Steer"), TEXT("[Left/Right | R stick]") },
		{ TEXT("IA_Sheet"), TEXT("[Up/Down | R stick]") },
		{ TEXT("IA_Edge"), TEXT("[A/D | L stick]") },
		{ TEXT("IA_WeightShift"), TEXT("[W/S | L stick]") },
		{ TEXT("IA_Jump"), TEXT("[Space | A]") },
		{ TEXT("IA_Reset"), TEXT("[R]") },
		{ TEXT("IA_Pause"), TEXT("[Esc | Start]") },
	};
	const FString* Found = Glyphs.Find(InputAction);
	return Found ? *Found : FString();
}

FString LessonHUD::FormatMetricValue(ELessonMetric Metric, ELessonChannel Channel, float Value)
{
	using namespace LessonHUDPrivate;
	switch (Metric)
	{
	case ELessonMetric::JumpHeight:
	case ELessonMetric::JumpDistance:
	case ELessonMetric::UpwindGain:
	case ELessonMetric::DistanceRidden:
		return FString::Printf(TEXT("%.1f m"), Value);
	case ELessonMetric::Airtime:
	case ELessonMetric::TimeToPlaning:
	case ELessonMetric::DiveBeforeTouchdown:
	case ELessonMetric::LoopStartSinceApex:
	case ELessonMetric::LoopDuration:
	case ELessonMetric::GrabHoldSeconds:
	case ELessonMetric::TransitionNotPlaningSeconds:
	case ELessonMetric::KiteLeadAtTransition:
		return FString::Printf(TEXT("%.1f s"), Value);
	case ELessonMetric::TakeoffSpeed:
	case ELessonMetric::SpeedHeld:
		return FString::Printf(TEXT("%.1f kn"), Value / MSPerKnot);
	case ELessonMetric::SinkAtTouchdown:
		return FString::Printf(TEXT("%.1f m/s"), Value);
	case ELessonMetric::LandingG:
		return FString::Printf(TEXT("%.1f g"), Value);
	case ELessonMetric::LandingGrade:
		return GradeName(Value);
	case ELessonMetric::KiteElevationAtTouchdown:
	case ELessonMetric::MinKiteElevationInAir:
	case ELessonMetric::KiteElevationHeld:
	case ELessonMetric::RotationDeg:
	case ELessonMetric::HeadingChange:
		return FString::Printf(TEXT("%.0f°"), Value);
	case ELessonMetric::TransitionSpeedKept:
		return FString::Printf(TEXT("%.0f%%"), Value * 100.0f);
	case ELessonMetric::Channel:
	case ELessonMetric::ChannelHeld:
		return ChannelValue(Channel, Value);
	default:
		return FString::Printf(TEXT("%.0f"), Value);
	}
}

FString LessonHUD::FormatTarget(const FLessonObjective& Objective)
{
	using namespace LessonHUDPrivate;
	const bool bMin = HasMin(Objective.Min);
	const bool bMax = HasMax(Objective.Max);
	if (Objective.Metric == ELessonMetric::LandingGrade)
	{
		return bMax ? FString::Printf(TEXT("%s or better"), *GradeName(Objective.Max)) : FString();
	}
	const ELessonChannel Channel = LessonEval::GetMetricSource(Objective.Metric) == ELessonMetricSource::Held ? LessonEval::HeldChannel(Objective) : Objective.Channel;
	auto Format = [&](float V) { return FormatMetricValue(Objective.Metric, Channel, V); };
	if (bMin && bMax)
	{
		// "1.0 - 2.0 m": the unit once, on the upper bound.
		FString Low = Format(Objective.Min);
		const FString High = Format(Objective.Max);
		int32 Space = INDEX_NONE;
		if (Low.FindLastChar(TEXT(' '), Space))
		{
			Low.LeftInline(Space);
		}
		else if (Low.Len() > 1 && !FChar::IsDigit(Low[Low.Len() - 1]))
		{
			Low.LeftChopInline(1); // "45°" -> "45"
		}
		return FString::Printf(TEXT("%s - %s"), *Low, *High);
	}
	if (bMin)
	{
		return FString::Printf(TEXT("at least %s"), *Format(Objective.Min));
	}
	if (bMax)
	{
		return FString::Printf(TEXT("at most %s"), *Format(Objective.Max));
	}
	return FString();
}

FString LessonHUD::FormatProgress(const FLessonObjective& Objective, float Progress)
{
	const float P = FMath::Clamp(Progress, 0.0f, 1.0f);
	if (LessonEval::GetMetricSource(Objective.Metric) == ELessonMetricSource::Held && Objective.WindowSeconds > 0.0f)
	{
		return FString::Printf(TEXT("%.1f / %.1f s"), P * Objective.WindowSeconds, Objective.WindowSeconds);
	}
	const int32 Count = FMath::Max(Objective.Count, 1);
	if (Count > 1)
	{
		return FString::Printf(TEXT("%d / %d"), FMath::RoundToInt(P * Count), Count);
	}
	return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(P * 100.0f));
}

FString LessonHUD::FormatHeader(const FLessonHUDInput& In)
{
	const FString Name = FString::Printf(TEXT("%s  %s"), *In.LessonId.ToString(), *In.Title.ToString());
	switch (In.Phase)
	{
	case ELessonPhase::Intro:
		return FString::Printf(TEXT("LESSON %s"), *Name);
	case ELessonPhase::Step:
		return FString::Printf(TEXT("%s   STEP %d/%d"), *Name, In.StepIndex + 1, FMath::Max(In.StepCount, 1));
	case ELessonPhase::Result:
		if (In.Outcome == ELessonOutcome::AttemptFailed)
		{
			return FString::Printf(TEXT("%s   STEP %d/%d"), *Name, In.StepIndex + 1, FMath::Max(In.StepCount, 1));
		}
		return Name;
	default:
		return FString();
	}
}

FString LessonHUD::FormatDropBack(const FLessonHUDInput& In)
{
	if (!In.bDropBackOffered)
	{
		return FString();
	}
	const FString Key = GlyphText(TEXT("IA_Reset"));
	if (In.StepIndex > 0)
	{
		return FString::Printf(TEXT("Too hard? Drop back to step %d  %s"), In.StepIndex, *Key);
	}
	if (!In.DropBackLessonId.IsNone())
	{
		return FString::Printf(TEXT("Too hard? Drop back to lesson %s  %s"), *In.DropBackLessonId.ToString(), *Key);
	}
	return FString();
}

FString LessonHUD::FormatNextStar(int32 Stars, bool bPassedHigherBar, const FText& HigherBarText, int32 AssistsOn)
{
	if (Stars <= 0 || Stars >= FLessonProgressBook::MaxStars)
	{
		return FString();
	}
	if (Stars == 1 && !bPassedHigherBar && !HigherBarText.IsEmpty())
	{
		return AssistsOn > 0
			? FString::Printf(TEXT("Next star: %s, or fewer assists"), *HigherBarText.ToString())
			: FString::Printf(TEXT("Next star: %s"), *HigherBarText.ToString());
	}
	if (Stars == 1)
	{
		return TEXT("Next star: fewer assists");
	}
	return AssistsOn > 0 ? FString(TEXT("Next star: every assist off")) : FString::Printf(TEXT("Next star: %s"), *HigherBarText.ToString());
}

FString LessonHUD::FormatStars(int32 Stars)
{
	return FString::Printf(TEXT("STARS %d/%d"), FMath::Clamp(Stars, 0, FLessonProgressBook::MaxStars), FLessonProgressBook::MaxStars);
}

FString LessonHUD::FormatCardActions(bool bPassed, FName NextLessonId)
{
	TArray<FString> Parts;
	if (bPassed && !NextLessonId.IsNone())
	{
		Parts.Add(FString::Printf(TEXT("%s Next: %s"), *GlyphText(TEXT("IA_Jump")), *NextLessonId.ToString()));
	}
	Parts.Add(FString::Printf(TEXT("%s Retry"), *GlyphText(TEXT("IA_Reset"))));
	Parts.Add(FString::Printf(TEXT("%s Lesson menu"), *GlyphText(TEXT("IA_Pause"))));
	return FString::Join(Parts, TEXT("    "));
}

FString LessonHUD::HeldHintLine(const TArray<FLessonFault>& Faults, const FLessonObjective& Objective, const FLessonTelemetry& Telemetry, float Value)
{
	using namespace LessonHUDPrivate;
	TArray<FLessonFault> RideFaults;
	for (const FLessonFault& F : Faults)
	{
		if (!NeedsJump(F.Measure))
		{
			RideFaults.Add(F);
		}
	}
	if (const FLessonFault* Found = LessonEval::DiagnoseFault(RideFaults, Telemetry, FJumpRecord(), FLessonJumpExtras()))
	{
		return Found->Feedback.ToString();
	}
	const bool bLow = HasMin(Objective.Min) && Value < Objective.Min;
	const bool bHigh = HasMax(Objective.Max) && Value > Objective.Max;
	switch (Objective.Metric)
	{
	case ELessonMetric::SpeedHeld:
		if (bLow) { return TEXT("Too slow: bar in"); }
		if (bHigh) { return TEXT("Too fast: bar out"); }
		break;
	case ELessonMetric::KiteElevationHeld:
		if (bLow) { return TEXT("Kite too low: steer it up"); }
		if (bHigh) { return TEXT("Kite too high: steer it down"); }
		break;
	case ELessonMetric::ChannelHeld:
		if (Objective.Channel == ELessonChannel::EdgeAbs || Objective.Channel == ELessonChannel::Edge)
		{
			if (bLow) { return TEXT("Edge harder"); }
			if (bHigh) { return TEXT("Ease the edge"); }
		}
		break;
	default:
		break;
	}
	const FString Target = FormatTarget(Objective);
	// In its own band but a condition broke the hold (A3: the kite left 45 deg).
	return Target.IsEmpty() ? FString(TEXT("Hold it steady")) : FString::Printf(TEXT("Hold it steady: %s"), *Target);
}

FLessonHUDView LessonHUD::BuildView(const FLessonHUDInput& In, const FLessonHUDTimers& Timers)
{
	FLessonHUDView V;
	if (!In.IsRunning())
	{
		return V;
	}
	V.bVisible = true;

	if (In.IsResultCardUp())
	{
		V.bResultCard = true;
		V.bPassed = In.Outcome == ELessonOutcome::Passed;
		V.CardTitle = V.bPassed ? TEXT("LESSON PASSED") : TEXT("TIME UP");
		V.CardLesson = FString::Printf(TEXT("%s  %s"), *In.LessonId.ToString(), *In.Title.ToString());
		V.CardStars = V.bPassed ? FMath::Clamp(In.Stars, 0, FLessonProgressBook::MaxStars) : 0;
		V.CardStarsText = FormatStars(V.CardStars);
		const ELessonChannel Channel = In.bHasObjective && LessonEval::GetMetricSource(In.Objective.Metric) == ELessonMetricSource::Held
			? LessonEval::HeldChannel(In.Objective) : In.Objective.Channel;
		if (V.bPassed && In.bHasObjective)
		{
			V.CardResult = FString::Printf(TEXT("Result  %s"), *FormatMetricValue(In.Objective.Metric, Channel, In.Value));
			if (In.bPassedHigherBar && !In.HigherBarText.IsEmpty())
			{
				V.CardResult += FString::Printf(TEXT("   (%s)"), *In.HigherBarText.ToString());
			}
		}
		if (In.bHasRecord)
		{
			FString Best = FString::Printf(TEXT("Best  %d/%d stars"), In.BestStars, FLessonProgressBook::MaxStars);
			if (In.bHasBestValue)
			{
				const ELessonMetric Metric = In.BestValueMetric != ELessonMetric::None ? In.BestValueMetric : In.Objective.Metric;
				Best += FString::Printf(TEXT("   %s"), *FormatMetricValue(Metric, Channel, In.BestValue));
			}
			Best += FString::Printf(TEXT("   %d %s"), In.Passes, In.Passes == 1 ? TEXT("pass") : TEXT("passes"));
			V.CardBest = Best;
		}
		else
		{
			V.CardBest = TEXT("Best  --");
		}
		V.CardNextStar = V.bPassed ? FormatNextStar(In.Stars, In.bPassedHigherBar, In.HigherBarText, In.AssistsOn) : FString();
		V.CardActions = FormatCardActions(V.bPassed, In.NextLessonId);
		return V;
	}

	V.bPanel = true;
	V.Header = FormatHeader(In);
	if (In.Phase == ELessonPhase::Intro)
	{
		V.Prompt = In.Summary.IsEmpty() ? In.Title.ToString() : In.Summary.ToString();
		V.Progress = TEXT("Get ready");
		V.ProgressFraction = In.IntroSeconds > 0.0f ? FMath::Clamp(In.PhaseSeconds / In.IntroSeconds, 0.0f, 1.0f) : 1.0f;
		return V;
	}

	// A step, or the moment after a missed attempt (the step goes on underneath).
	V.Prompt = In.StepPrompt.ToString();
	V.Glyph = GlyphText(In.Glyph);
	if (In.bHasObjective)
	{
		const ELessonChannel Channel = LessonEval::GetMetricSource(In.Objective.Metric) == ELessonMetricSource::Held
			? LessonEval::HeldChannel(In.Objective) : In.Objective.Channel;
		TArray<FString> Parts;
		Parts.Add(FormatProgress(In.Objective, In.Progress));
		Parts.Add(FormatMetricValue(In.Objective.Metric, Channel, In.Value));
		const FString Target = FormatTarget(In.Objective);
		if (!Target.IsEmpty())
		{
			Parts.Add(FString::Printf(TEXT("target %s"), *Target));
		}
		V.Progress = FString::Join(Parts, TEXT("   "));
		V.ProgressFraction = FMath::Clamp(In.Progress, 0.0f, 1.0f);
	}
	if (Timers.bShowFault)
	{
		V.Fault = In.FaultLine.IsEmpty() ? FString(TEXT("Not quite: try again")) : In.FaultLine.ToString();
	}
	if (Timers.bShowHint && V.Fault.IsEmpty() && !In.HeldHint.IsEmpty())
	{
		V.Hint = In.HeldHint;
	}
	V.DropBack = FormatDropBack(In);
	if (Timers.bShowTiming && In.TimingGrade != ELessonTimingGrade::None)
	{
		V.Timing = LessonTiming::GradeText(In.TimingGrade);
		V.TimingGrade = In.TimingGrade;
		V.TimingAge = Timers.TimingAge;
	}
	return V;
}

// ---------------------------------------------------------------------------------------------
// Cue geometry
// ---------------------------------------------------------------------------------------------

FVector2D LessonHUD::ArcPoint(const FVector2D& Centre, float Radius, float ClockDeg, float DepthDeg)
{
	// As AKiteSurfHUD::DrawWindWindowArc places the kite: on the arc at the window edge, nearer the
	// middle the deeper it is in the window.
	const float Ring = Radius * FMath::Cos(FMath::DegreesToRadians(DepthDeg));
	const float Clock = FMath::DegreesToRadians(ClockDeg);
	return FVector2D(Centre.X + Ring * FMath::Sin(Clock), Centre.Y - Ring * FMath::Cos(Clock));
}

float LessonHUD::ElevationToClock(float ElevationDeg, int32 Side)
{
	return (Side < 0 ? -1.0f : 1.0f) * (90.0f - FMath::Clamp(ElevationDeg, 0.0f, 90.0f));
}

bool LessonHUD::ElevationBand(const FLessonObjective& Objective, float& OutMinDeg, float& OutMaxDeg)
{
	using namespace LessonHUDPrivate;
	auto Take = [&](float Min, float Max)
	{
		OutMinDeg = HasMin(Min) ? FMath::Clamp(Min, 0.0f, 90.0f) : 0.0f;
		OutMaxDeg = HasMax(Max) ? FMath::Clamp(Max, 0.0f, 90.0f) : 90.0f;
		return true;
	};
	if (Objective.Metric == ELessonMetric::KiteElevationHeld
		|| ((Objective.Metric == ELessonMetric::Channel || Objective.Metric == ELessonMetric::ChannelHeld) && Objective.Channel == ELessonChannel::KiteElevation))
	{
		return Take(Objective.Min, Objective.Max);
	}
	for (const FLessonCondition& C : Objective.Conditions)
	{
		if (C.Measure.Metric == ELessonMetric::Channel && C.Measure.Channel == ELessonChannel::KiteElevation)
		{
			return Take(C.Min, C.Max);
		}
	}
	return false;
}

TArray<FLessonArcZone> LessonHUD::ArcZones(ELessonCue Cue, const FLessonObjective* Objective, bool bSheetInStep, int32 Tack)
{
	TArray<FLessonArcZone> Zones;
	const float Side = Tack < 0 ? -1.0f : 1.0f;
	auto Add = [&Zones](float A, float B, ELessonArcZoneKind Kind)
	{
		FLessonArcZone Z;
		Z.FromClockDeg = FMath::Min(A, B);
		Z.ToClockDeg = FMath::Max(A, B);
		Z.Kind = Kind;
		Zones.Add(Z);
	};
	if (Cue == ELessonCue::WindowArc)
	{
		float MinDeg = 0.0f;
		float MaxDeg = 90.0f;
		if (Objective && ElevationBand(*Objective, MinDeg, MaxDeg))
		{
			const float Near = 90.0f - MaxDeg; // clock nearest 12
			const float Far = 90.0f - MinDeg;
			if (Near <= 0.0f)
			{
				Add(-Far, Far, ELessonArcZoneKind::Target);
			}
			else
			{
				Add(-Far, -Near, ELessonArcZoneKind::Target);
				Add(Near, Far, ELessonArcZoneKind::Target);
			}
			return Zones;
		}
		Add(-CruiseMaxDeg, -CruiseMinDeg, ELessonArcZoneKind::Cruise);
		Add(CruiseMinDeg, CruiseMaxDeg, ELessonArcZoneKind::Cruise);
		Add(-SheetInHalfClockDeg, SheetInHalfClockDeg, ELessonArcZoneKind::SheetIn);
		Add(Side * LandingFromClockDeg, Side * LandingToClockDeg, ELessonArcZoneKind::Landing);
		return Zones;
	}
	if (Cue == ELessonCue::TimingRing)
	{
		const bool bTransition = Objective && (Objective->Metric == ELessonMetric::Transition || Objective->Metric == ELessonMetric::TransitionNotPlaningSeconds
			|| Objective->Metric == ELessonMetric::TransitionSpeedKept || Objective->Metric == ELessonMetric::KiteLeadAtTransition);
		if (bSheetInStep || bTransition)
		{
			Add(-SheetInHalfClockDeg, SheetInHalfClockDeg, ELessonArcZoneKind::SheetIn);
		}
	}
	return Zones;
}

void LessonHUD::GhostTarget(const FLessonObjective& Objective, float KiteElevationDeg, float KiteClockDeg, int32 Tack, float& OutClockDeg, float& OutElevationDeg)
{
	const int32 KiteSide = FMath::Abs(KiteClockDeg) > 1.0f ? (KiteClockDeg < 0.0f ? -1 : 1) : (Tack < 0 ? -1 : 1);
	auto At = [&](float Elevation, int32 Side)
	{
		OutElevationDeg = Elevation;
		OutClockDeg = ElevationToClock(Elevation, Side);
	};
	float MinDeg = 0.0f;
	float MaxDeg = 90.0f;
	const bool bBand = ElevationBand(Objective, MinDeg, MaxDeg);
	const bool bClockNearTop = Objective.Metric == ELessonMetric::Channel && Objective.Channel == ELessonChannel::KiteClockAbs
		&& LessonHUDPrivate::HasMax(Objective.Max) && Objective.Max <= 20.0f;
	if ((bBand && MinDeg >= 75.0f) || bClockNearTop)
	{
		At(85.0f, KiteSide);
		return;
	}
	if (Objective.Metric == ELessonMetric::KiteDives || Objective.Metric == ELessonMetric::TimeToPlaning)
	{
		// The dive: down to 45 while the kite is high, then straight back up.
		At(KiteElevationDeg > 50.0f ? 45.0f : 80.0f, KiteSide);
		return;
	}
	if (bBand)
	{
		At(0.5f * (MinDeg + MaxDeg), KiteSide);
		return;
	}
	At(85.0f, KiteSide);
}

float LessonHUD::TimingRingRadius(float KiteClockDeg, float MarkerRadius)
{
	const float Away = FMath::Clamp(FMath::Abs(KiteClockDeg) / 60.0f, 0.0f, 1.0f);
	return MarkerRadius * (1.0f + 3.0f * Away);
}

float LessonHUD::ZoneAlpha(int32 BestStars)
{
	return FMath::Lerp(1.0f, 0.4f, FMath::Clamp(BestStars, 0, FLessonProgressBook::MaxStars) / float(FLessonProgressBook::MaxStars));
}

// ---------------------------------------------------------------------------------------------
// Layer
// ---------------------------------------------------------------------------------------------

void FLessonHUDLayer::Update(const ALessonDirector* Director, float DeltaSeconds)
{
	UpdateFromInput(Director ? FLessonHUDInput::FromDirector(*Director) : FLessonHUDInput(), DeltaSeconds);
}

void FLessonHUDLayer::UpdateFromInput(const FLessonHUDInput& In, float DeltaSeconds)
{
	const float Dt = FMath::Max(DeltaSeconds, 0.0f);
	const bool bNewLesson = In.LessonId != LastLessonId || In.StepIndex != LastStepIndex || (In.Phase == ELessonPhase::Intro && LastPhase != ELessonPhase::Intro);
	if (bNewLesson)
	{
		HintTimer.Reset();
		FaultRemaining = 0.0f;
	}
	if (In.LessonId != LastLessonId || In.TimingSerial < SeenTimingSerial)
	{
		// A new director, or a new run: its grades count from where it starts.
		SeenTimingSerial = In.TimingSerial;
		TimingAge = UE_BIG_NUMBER;
	}

	// The fault line: from the moment an attempt is missed, for FaultLineSeconds.
	const bool bMissed = In.Phase == ELessonPhase::Result && In.Outcome == ELessonOutcome::AttemptFailed;
	const bool bWasMissed = LastPhase == ELessonPhase::Result && LastOutcome == ELessonOutcome::AttemptFailed;
	if (bMissed && !bWasMissed)
	{
		FaultRemaining = LessonHUD::FaultLineSeconds;
	}
	else
	{
		FaultRemaining = FMath::Max(0.0f, FaultRemaining - Dt);
	}
	if (!In.IsRunning() || In.IsResultCardUp())
	{
		FaultRemaining = 0.0f;
	}

	// The hint: a held objective out of its band for a while, in a step.
	const bool bHeld = In.Phase == ELessonPhase::Step && In.bHasObjective && LessonEval::GetMetricSource(In.Objective.Metric) == ELessonMetricSource::Held;
	const bool bShowHint = HintTimer.Update(bHeld, In.bInBand, Dt);

	// The timing flash: each new grade once.
	if (In.TimingSerial != SeenTimingSerial)
	{
		SeenTimingSerial = In.TimingSerial;
		TimingAge = 0.0f;
	}
	else if (TimingAge < UE_BIG_NUMBER)
	{
		TimingAge += Dt;
	}

	FLessonHUDTimers Timers;
	Timers.bShowFault = FaultRemaining > 0.0f;
	Timers.bShowHint = bShowHint;
	Timers.bShowTiming = TimingAge < LessonHUD::TimingFlashSeconds;
	Timers.TimingAge = TimingAge;

	Input = In;
	View = LessonHUD::BuildView(In, Timers);
	LastPhase = In.Phase;
	LastOutcome = In.Outcome;
	LastLessonId = In.LessonId;
	LastStepIndex = In.StepIndex;
}

float FLessonHUDLayer::DrawPanel(AHUD& HUD, float ScreenW, float ScreenH, float Top) const
{
	using namespace LessonHUDPrivate;
	if (!View.bPanel)
	{
		return Top;
	}
	const float Ui = UiScale(ScreenH);
	const float Pad = 14.0f * Ui;
	// Centred, clear of the telemetry panel on the left and the wind flag on the right.
	const float LeftLimit = 365.0f;
	const float RightLimit = FMath::Max(ScreenW - 185.0f, LeftLimit + 200.0f);
	const float BoxW = FMath::Min(760.0f * Ui, RightLimit - LeftLimit);
	const float BoxX = FMath::Clamp(ScreenW * 0.5f - BoxW * 0.5f, LeftLimit, RightLimit - BoxW);
	const float InnerW = BoxW - 2.0f * Pad;

	struct FLine { FString Text; FLinearColor Ink; float Scale; float W = 0.0f; float H = 0.0f; bool bBar = false; };
	TArray<FLine> Lines;
	auto Add = [&](const FString& Text, const FLinearColor& Ink, float Scale)
	{
		if (Text.IsEmpty())
		{
			return;
		}
		FLine& L = Lines.AddDefaulted_GetRef();
		L.Text = Text;
		L.Ink = Ink;
		L.Scale = Scale * Ui;
		HUD.GetTextSize(L.Text, L.W, L.H, nullptr, L.Scale);
		// Shrink a line that would overflow the box rather than spill out of it.
		if (L.W > InnerW && L.W > 0.0f)
		{
			L.Scale *= InnerW / L.W;
			HUD.GetTextSize(L.Text, L.W, L.H, nullptr, L.Scale);
		}
	};
	const bool bFault = !View.Fault.IsEmpty();
	const FLinearColor Accent = bFault ? FLinearColor(1.0f, 0.35f, 0.35f) : (View.Glyph.IsEmpty() ? FLinearColor(1.0f, 0.85f, 0.2f) : FLinearColor(0.2f, 0.85f, 1.0f));
	Add(View.Header, Accent, 0.95f);
	Add(View.Prompt, bFault ? FLinearColor(0.75f, 0.8f, 0.85f) : FLinearColor::White, 1.4f);
	Add(View.Glyph, FLinearColor(1.0f, 0.85f, 0.2f), 1.05f);
	Add(View.Fault, FLinearColor(1.0f, 0.45f, 0.35f), 1.3f);
	const int32 BarIndex = Lines.Num();
	Add(View.Progress, FLinearColor(0.8f, 0.9f, 1.0f), 0.95f);
	Add(View.Hint, FLinearColor(1.0f, 0.7f, 0.25f), 1.1f);
	Add(View.DropBack, FLinearColor(0.55f, 0.9f, 1.0f), 1.05f);

	const float Spacing = 6.0f * Ui;
	const float BarH = 7.0f * Ui;
	float ContentH = 0.0f;
	for (const FLine& L : Lines)
	{
		ContentH += L.H + Spacing;
	}
	ContentH += BarH + Spacing;
	const float BoxH = ContentH + 2.0f * Pad - Spacing;
	HUD.DrawRect(FLinearColor(0.02f, 0.06f, 0.12f, 0.85f), BoxX, Top, BoxW, BoxH);
	HUD.DrawRect(FLinearColor(Accent.R, Accent.G, Accent.B, 0.9f), BoxX, Top, BoxW, 3.0f * Ui);

	float Y = Top + Pad;
	for (int32 I = 0; I < Lines.Num(); ++I)
	{
		if (I == BarIndex)
		{
			// The progress bar sits above its numbers.
			HUD.DrawRect(FLinearColor(0.15f, 0.2f, 0.25f, 0.9f), BoxX + Pad, Y, InnerW, BarH);
			HUD.DrawRect(FLinearColor(0.2f, 0.85f, 1.0f, 1.0f), BoxX + Pad, Y, InnerW * View.ProgressFraction, BarH);
			Y += BarH + Spacing;
		}
		const FLine& L = Lines[I];
		HUD.DrawText(L.Text, L.Ink, BoxX + Pad, Y, nullptr, L.Scale);
		Y += L.H + Spacing;
	}
	if (BarIndex >= Lines.Num())
	{
		HUD.DrawRect(FLinearColor(0.15f, 0.2f, 0.25f, 0.9f), BoxX + Pad, Y, InnerW, BarH);
		HUD.DrawRect(FLinearColor(0.2f, 0.85f, 1.0f, 1.0f), BoxX + Pad, Y, InnerW * View.ProgressFraction, BarH);
	}
	float Bottom = Top + BoxH;

	// The timing grade, under the panel: it pops in large and settles, then fades.
	if (!View.Timing.IsEmpty())
	{
		const float Pop = 1.0f + 0.5f * FMath::Clamp(1.0f - View.TimingAge / 0.15f, 0.0f, 1.0f);
		const float Alpha = FMath::Clamp((LessonHUD::TimingFlashSeconds - View.TimingAge) / 0.3f, 0.0f, 1.0f);
		const float Scale = 2.0f * Ui * Pop;
		float W = 0.0f;
		float H = 0.0f;
		HUD.GetTextSize(View.Timing, W, H, nullptr, Scale);
		const float TX = BoxX + BoxW * 0.5f - W * 0.5f;
		const float TY = Bottom + 8.0f * Ui;
		HUD.DrawRect(FLinearColor(0.02f, 0.05f, 0.1f, 0.7f * Alpha), TX - 14.0f * Ui, TY - 4.0f * Ui, W + 28.0f * Ui, H + 8.0f * Ui);
		HUD.DrawText(View.Timing, TimingColor(View.TimingGrade, Alpha), TX, TY, nullptr, Scale);
		Bottom = TY + H + 4.0f * Ui;
	}
	return Bottom;
}

void FLessonHUDLayer::DrawResultCard(AHUD& HUD, float ScreenW, float ScreenH) const
{
	using namespace LessonHUDPrivate;
	if (!View.bResultCard)
	{
		return;
	}
	const float Ui = UiScale(ScreenH);
	const FLinearColor Gold(1.0f, 0.85f, 0.2f);
	const FLinearColor Accent = View.bPassed ? Gold : FLinearColor(1.0f, 0.4f, 0.35f);
	struct FLine { FString Text; FLinearColor Ink; float Scale; float W = 0.0f; float H = 0.0f; bool bStars = false; };
	TArray<FLine> Lines;
	auto Add = [&](const FString& Text, const FLinearColor& Ink, float Scale, bool bStars = false)
	{
		if (Text.IsEmpty())
		{
			return;
		}
		FLine& L = Lines.AddDefaulted_GetRef();
		L.Text = Text;
		L.Ink = Ink;
		L.Scale = Scale * Ui;
		L.bStars = bStars;
		if (bStars)
		{
			L.W = 3.0f * 56.0f * Ui;
			L.H = 50.0f * Ui;
		}
		else
		{
			HUD.GetTextSize(L.Text, L.W, L.H, nullptr, L.Scale);
		}
	};
	Add(View.CardTitle, Accent, 1.8f);
	Add(View.CardLesson, FLinearColor::White, 1.3f);
	Add(View.CardStarsText, Gold, 1.0f, true);
	Add(View.CardResult, FLinearColor(0.85f, 0.9f, 0.95f), 1.2f);
	Add(View.CardBest, FLinearColor(0.55f, 0.9f, 1.0f), 1.1f);
	Add(View.CardNextStar, FLinearColor(0.8f, 0.85f, 0.9f), 1.0f);
	Add(View.CardActions, Gold, 1.15f);

	const float Spacing = 10.0f * Ui;
	float CardW = 0.0f;
	float CardH = 0.0f;
	for (const FLine& L : Lines)
	{
		CardW = FMath::Max(CardW, L.W);
		CardH += L.H + Spacing;
	}
	CardH -= Spacing;
	const float Pad = 26.0f * Ui;
	CardW = FMath::Min(CardW, ScreenW - 2.0f * Pad - 32.0f);
	const float Top = FMath::Max(ScreenH * 0.26f, 16.0f + Pad);
	const float Left = ScreenW * 0.5f - CardW * 0.5f - Pad;
	HUD.DrawRect(FLinearColor(0.01f, 0.03f, 0.08f, 0.88f), Left, Top - Pad, CardW + 2.0f * Pad, CardH + 2.0f * Pad);
	HUD.DrawRect(FLinearColor(Accent.R, Accent.G, Accent.B, 0.9f), Left, Top - Pad, CardW + 2.0f * Pad, 3.0f * Ui);
	float Y = Top;
	for (const FLine& L : Lines)
	{
		if (L.bStars)
		{
			const float R = 22.0f * Ui;
			const float Step = 56.0f * Ui;
			for (int32 I = 0; I < FLessonProgressBook::MaxStars; ++I)
			{
				const FVector2D C(ScreenW * 0.5f + (I - 1) * Step, Y + L.H * 0.5f);
				const bool bFilled = I < View.CardStars;
				DrawStar(HUD, C, R, bFilled, bFilled ? Gold : FLinearColor(0.45f, 0.5f, 0.55f, 0.9f));
			}
		}
		else
		{
			HUD.DrawText(L.Text, L.Ink, ScreenW * 0.5f - L.W * 0.5f, Y, nullptr, L.Scale);
		}
		Y += L.H + Spacing;
	}
}

void FLessonHUDLayer::DrawArcCue(AHUD& HUD, const FVector2D& Centre, float Radius) const
{
	using namespace LessonHUDPrivate;
	if (!View.bPanel || Input.Phase == ELessonPhase::Intro)
	{
		return;
	}
	const float Alpha = LessonHUD::ZoneAlpha(Input.BestStars);
	const TArray<FLessonArcZone> Zones = LessonHUD::ArcZones(Input.Cue, Input.bHasObjective ? &Input.Objective : nullptr, Input.bSheetInStep, Input.Tack);
	for (const FLessonArcZone& Z : Zones)
	{
		DrawArcBand(HUD, Centre, Radius + 5.0f, Z.FromClockDeg, Z.ToClockDeg, ZoneColor(Z.Kind, Alpha), 6.0f);
	}
	const FVector2D Kite = LessonHUD::ArcPoint(Centre, Radius, Input.KiteClockDeg, Input.KiteDepthDeg);

	if (Input.Cue == ELessonCue::GhostKite && Input.bHasObjective)
	{
		// A translucent kite at the target: a C-shaped canopy of lines over a short bridle.
		float Clock = 0.0f;
		float Elevation = 85.0f;
		LessonHUD::GhostTarget(Input.Objective, Input.KiteElevationDeg, Input.KiteClockDeg, Input.Tack, Clock, Elevation);
		const FVector2D G = LessonHUD::ArcPoint(Centre, Radius, Clock);
		const FVector2D Out = (G - Centre).GetSafeNormal();
		const FVector2D Across(-Out.Y, Out.X);
		const FLinearColor Ghost(0.85f, 0.95f, 1.0f, 0.55f);
		const float Span = 13.0f;
		FVector2D Prev = G - Across * Span;
		for (int32 I = 1; I <= 8; ++I)
		{
			const float T = -1.0f + 2.0f * I / 8.0f;
			const FVector2D P = G + Across * (Span * T) + Out * (5.0f * (1.0f - T * T));
			HUD.DrawLine(Prev.X, Prev.Y, P.X, P.Y, Ghost, 4.0f);
			Prev = P;
		}
		const FVector2D Bridle = G - Out * 12.0f;
		HUD.DrawLine(G.X - Across.X * Span, G.Y - Across.Y * Span, Bridle.X, Bridle.Y, FLinearColor(0.85f, 0.95f, 1.0f, 0.35f), 1.0f);
		HUD.DrawLine(G.X + Across.X * Span, G.Y + Across.Y * Span, Bridle.X, Bridle.Y, FLinearColor(0.85f, 0.95f, 1.0f, 0.35f), 1.0f);
		// A dotted guide from the kite to the ghost.
		for (float T = 0.1f; T < 0.95f; T += 0.15f)
		{
			const FVector2D P = FMath::Lerp(Kite, G, T);
			HUD.DrawRect(FLinearColor(0.85f, 0.95f, 1.0f, 0.4f), P.X - 1.5f, P.Y - 1.5f, 3.0f, 3.0f);
		}
	}

	if (Input.Cue == ELessonCue::TimingRing)
	{
		// A ring around the kite marker that closes as the kite nears 12, green inside the 12 band.
		const float Ring = LessonHUD::TimingRingRadius(Input.KiteClockDeg, 9.0f);
		const bool bOn = FMath::Abs(Input.KiteClockDeg) <= LessonHUD::SheetInHalfClockDeg;
		const FLinearColor Ink = bOn ? FLinearColor(0.35f, 1.0f, 0.45f, 0.95f) : FLinearColor(1.0f, 0.85f, 0.2f, 0.85f);
		const int32 Segments = 24;
		for (int32 I = 0; I < Segments; ++I)
		{
			const float A0 = 2.0f * UE_PI * I / Segments;
			const float A1 = 2.0f * UE_PI * (I + 1) / Segments;
			HUD.DrawLine(Kite.X + Ring * FMath::Cos(A0), Kite.Y + Ring * FMath::Sin(A0), Kite.X + Ring * FMath::Cos(A1), Kite.Y + Ring * FMath::Sin(A1), Ink, 2.0f);
		}
	}
}

void FLessonHUDLayer::DrawSpeedBand(AHUD& HUD, float X, float Y, float Width) const
{
	using namespace LessonHUDPrivate;
	if (!View.bPanel || Input.Cue != ELessonCue::SpeedBand || !Input.bHasObjective)
	{
		return;
	}
	const float Min = HasMin(Input.Objective.Min) ? Input.Objective.Min : 0.0f;
	const float Max = HasMax(Input.Objective.Max) ? Input.Objective.Max : Min * 1.5f;
	const float Range = FMath::Max3(Max * 1.5f, Min + 2.0f, 6.0f);
	const float H = 58.0f;
	HUD.DrawRect(FLinearColor(0.02f, 0.05f, 0.1f, 0.75f), X, Y, Width, H);
	const bool bIn = Input.SpeedMS >= Min && Input.SpeedMS <= Max;
	HUD.DrawText(FString::Printf(TEXT("TARGET SPEED  %.1f - %.1f kn"), Min / MSPerKnot, Max / MSPerKnot), FLinearColor(1.0f, 0.85f, 0.2f), X + 10.0f, Y + 5.0f, nullptr, 1.0f);
	HUD.DrawText(FString::Printf(TEXT("%.1f kn"), Input.SpeedMS / MSPerKnot), bIn ? FLinearColor(0.4f, 1.0f, 0.5f) : FLinearColor(1.0f, 0.6f, 0.2f), X + Width - 70.0f, Y + 5.0f, nullptr, 1.0f);
	const float TrackX = X + 10.0f;
	const float TrackW = Width - 20.0f;
	const float TrackY = Y + 32.0f;
	HUD.DrawRect(FLinearColor(0.1f, 0.12f, 0.15f, 0.9f), TrackX, TrackY, TrackW, 12.0f);
	HUD.DrawRect(FLinearColor(0.35f, 1.0f, 0.45f, 0.7f), TrackX + TrackW * Min / Range, TrackY, TrackW * (Max - Min) / Range, 12.0f);
	const float NeedleX = TrackX + TrackW * FMath::Clamp(Input.SpeedMS / Range, 0.0f, 1.0f);
	HUD.DrawRect(FLinearColor::White, NeedleX - 2.0f, TrackY - 5.0f, 4.0f, 22.0f);
}
