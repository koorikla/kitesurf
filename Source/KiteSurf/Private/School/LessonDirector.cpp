#include "School/LessonDirector.h"

#include "BoardMovementComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "KiteComponent.h"
#include "KiteRiderPawn.h"
#include "KiteSurfGameMode.h"
#include "KiteSurfUnits.h"
#include "School/LessonCatalog.h"
#include "School/LessonSubsystem.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/RiderAttitudeComponent.h"
#include "Tricks/TrickTrackerComponent.h"
#include "WindComponent.h"

#include <limits>

// Named, not anonymous, so a unity build cannot merge these helpers with another file's.
namespace LessonDirectorPrivate
{
	/** Fixed steps per telemetry sample: 240 Hz / 4 = 60 Hz (docs/tutorials.md 3.3, LessonTelemetry.h). */
	constexpr int32 StepsPerSample = 4;
	/** Below this the rider is going nowhere and has no tack (m/s). */
	constexpr float MinTackSpeedMS = 0.5f;
	/** Kite depth for a "kite at 12" start (deg from the window edge): InitializeRide's own start depth. */
	constexpr float ZenithStartDepthDeg = AKiteSurfGameMode::StartKiteDepthDeg;

	/** A measure that can only be read with a jump record. */
	bool NeedsJump(const FLessonMeasure& Measure)
	{
		if (LessonEval::GetMetricSource(Measure.Metric) == ELessonMetricSource::Jump)
		{
			return true;
		}
		return Measure.Metric == ELessonMetric::Channel
			&& (Measure.Anchor == ELessonAnchor::Takeoff || Measure.Anchor == ELessonAnchor::Apex || Measure.Anchor == ELessonAnchor::Touchdown);
	}

	FString AssistsText(const FLessonAssists& A)
	{
		TArray<FString> On;
		if (A.bAutoPark) { On.Add(TEXT("auto-park")); }
		if (A.bAutoEdge) { On.Add(TEXT("auto-edge")); }
		if (A.bLandingAssist) { On.Add(TEXT("landing assist")); }
		if (A.bAutoRedirect) { On.Add(TEXT("auto-redirect")); }
		if (A.bLoopCatch) { On.Add(TEXT("loop catch")); }
		if (A.bSlowMotion) { On.Add(TEXT("slow motion")); }
		return On.Num() > 0 ? FString::Join(On, TEXT(", ")) : FString(TEXT("none"));
	}
}

ALessonDirector::ALessonDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	// After the pawn, which steps the ride in its own (pre-physics) tick; BeginLesson also makes the
	// pawn a prerequisite. The telemetry is read from the public getters, never from inside the step.
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetCanBeDamaged(false);
}

void ALessonDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateLesson(DeltaSeconds);
}

void ALessonDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Destroyed on purpose (ExitToFreeRide, a new lesson in StartInWorld): put the rider's free-ride
	// gear and assists back. A level change or quit takes the rider with it, so leave it be.
	if (EndPlayReason == EEndPlayReason::Destroyed && Phase != ELessonPhase::Idle)
	{
		RecordAbandonedAttempt();
		RestoreSnapshot();
		Phase = ELessonPhase::Idle;
	}
	Super::EndPlay(EndPlayReason);
}

ELessonStart ALessonDirector::SupportedStart(ELessonStart Requested)
{
	switch (Requested)
	{
	case ELessonStart::Standing:
		// Standing in the shallows does not exist (docs/tutorials.md section 4): float instead.
		return ELessonStart::Floating;
	case ELessonStart::Airborne:
		// The board has no public way to start a jump in mid-air that the trick tracker would record
		// (take-offs are counted inside the board's step): ride instead, at the lesson's speed.
		return ELessonStart::Riding;
	default:
		return Requested;
	}
}

bool ALessonDirector::BeginLessonById(FName LessonId, APawn* Pawn)
{
	const FLessonDef* Def = LessonCatalog::Find(LessonId);
	return Def && BeginLesson(*Def, Pawn);
}

bool ALessonDirector::BeginLesson(const FLessonDef& InLesson, APawn* Pawn)
{
	return BeginLesson(InLesson, Pawn, FLessonRunOptions());
}

bool ALessonDirector::BeginLesson(const FLessonDef& InLesson, APawn* Pawn, const FLessonRunOptions& Options)
{
	AKiteRiderPawn* NewRider = Cast<AKiteRiderPawn>(Pawn);
	if (!NewRider || !NewRider->GetBoardMovement() || !NewRider->GetKite() || InLesson.Steps.Num() == 0 || !InLesson.Pass.IsSet())
	{
		UE_LOG(LogKiteSchool, Warning, TEXT("Lesson %s: cannot begin (no kite rider, or the lesson has no steps or pass test)"), *InLesson.Id.ToString());
		return false;
	}
	// Copy first: InLesson may be this director's own Lesson (Retry).
	const FLessonDef NewLesson = InLesson;
	const FLessonRunOptions NewOptions = Options;

	if (Phase != ELessonPhase::Idle)
	{
		RecordAbandonedAttempt();
	}
	if (Rider.Get() != NewRider)
	{
		RestoreSnapshot();
		Snapshot.bValid = false;
		if (AKiteRiderPawn* OldRider = Rider.Get())
		{
			RemoveTickPrerequisiteActor(OldRider);
		}
	}
	Rider = NewRider;
	if (!Snapshot.bValid)
	{
		TakeSnapshot();
	}

	Lesson = NewLesson;
	RunOptions = NewOptions;
	UsedAssists = RunOptions.bOverrideAssists ? RunOptions.Assists : Lesson.Setup.Assists;
	WindKnots = FMath::Max(Lesson.Setup.WindKnots, RunOptions.WindKnots);

	ApplySetup();

	Telemetry.Reset();
	NextSampleTime = -UE_BIG_NUMBER;
	const UTrickTrackerComponent* Tracker = NewRider->GetTrickTracker();
	LastSeenRecordCount = Tracker ? Tracker->GetJumpRecordCount() : 0;
	SeenLoopRecords = NewRider->GetKite()->GetLoopRecordCount();
	CompletedLoops = 0;
	LessonAttempts = 0;
	Stars = 0;
	bPassedHigherBar = false;
	bRecorded = false;
	LastFaultLine = FText::GetEmpty();
	LastFaultId = NAME_None;
	Outcome = ELessonOutcome::None;
	StepIndex = 0;
	TimingGrade = ELessonTimingGrade::None;
	LastSheetGradeTime = -UE_BIG_NUMBER;

	AddTickPrerequisiteActor(NewRider);
	SetActorTickEnabled(true);

	const UKiteComponent* Kite = NewRider->GetKite();
	UE_LOG(LogKiteSchool, Display, TEXT("Lesson %s (%s): begin. Wind %.0f kn, kite %s %.0f m2, board %s, start %s%s, assists: %s"),
		*Lesson.Id.ToString(), *Lesson.Title.ToString(), WindKnots,
		*UEnum::GetValueAsString(Kite->GetKiteModel()), Kite->AreaM2,
		*UEnum::GetValueAsString(NewRider->GetBoardMovement()->GetBoardSize()),
		*UEnum::GetValueAsString(AppliedStart),
		AppliedStart == ELessonStart::Riding ? *FString::Printf(TEXT(" at %.0f kn, tack %+d"), Lesson.Setup.StartSpeedKnots, Lesson.Setup.StartTack >= 0 ? 1 : -1) : TEXT(""),
		*LessonDirectorPrivate::AssistsText(UsedAssists));
	EnterIntro();
	return true;
}

void ALessonDirector::TakeSnapshot()
{
	AKiteRiderPawn* P = Rider.Get();
	if (!P)
	{
		return;
	}
	Snapshot = FFreeRideSnapshot();
	if (const UWindComponent* Wind = P->GetWind())
	{
		Snapshot.BaseWind = Wind->BaseWind;
	}
	if (const UKiteComponent* Kite = P->GetKite())
	{
		Snapshot.KiteModel = Kite->GetKiteModel();
		Snapshot.KiteSizeM2 = Kite->AreaM2;
		Snapshot.bParkHoldAssist = Kite->bParkHoldAssist;
	}
	if (const UBoardMovementComponent* Board = P->GetBoardMovement())
	{
		Snapshot.BoardSize = Board->GetBoardSize();
		Snapshot.bAutoEdge = Board->bAutoEdge;
	}
	if (const URiderAttitudeComponent* Attitude = P->GetRiderAttitude())
	{
		Snapshot.LandingAssistStrength = Attitude->AssistStrength;
	}
	Snapshot.bValid = true;
}

void ALessonDirector::RestoreSnapshot()
{
	AKiteRiderPawn* P = Rider.Get();
	if (!P || !Snapshot.bValid)
	{
		return;
	}
	if (UWindComponent* Wind = P->GetWind())
	{
		Wind->BaseWind = Snapshot.BaseWind;
	}
	if (UKiteComponent* Kite = P->GetKite())
	{
		Kite->SetKiteModel(Snapshot.KiteModel);
		Kite->SetKiteSize(Snapshot.KiteSizeM2);
		Kite->bParkHoldAssist = Snapshot.bParkHoldAssist;
	}
	if (UBoardMovementComponent* Board = P->GetBoardMovement())
	{
		Board->SetBoardSize(Snapshot.BoardSize);
		Board->bAutoEdge = Snapshot.bAutoEdge;
	}
	if (URiderAttitudeComponent* Attitude = P->GetRiderAttitude())
	{
		Attitude->AssistStrength = Snapshot.LandingAssistStrength;
	}
}

void ALessonDirector::ApplySetup()
{
	AKiteRiderPawn* P = Rider.Get();
	UBoardMovementComponent* Board = P ? P->GetBoardMovement() : nullptr;
	UKiteComponent* Kite = P ? P->GetKite() : nullptr;
	if (!Board || !Kite)
	{
		return;
	}
	const FLessonSetup& Setup = Lesson.Setup;
	AppliedStart = SupportedStart(Setup.Start);
	const bool bFloating = AppliedStart == ELessonStart::Floating;
	const float StartKnots = bFloating ? 0.0f : FMath::Max(Setup.StartSpeedKnots, 0.0f);
	const float Tack = Setup.StartTack >= 0 ? 1.0f : -1.0f;

	// The start. The board reset clears a crash and puts the board on the water at the float depth
	// for the speed; the ride start then points it across the wind on the tack with the kite
	// powered up on that side, as a free ride starts.
	Board->ResetToTack(StartKnots);
	AKiteSurfGameMode::InitializeRide(P, KiteUnits::KnotsToCmS(StartKnots), Tack);
	if (bFloating)
	{
		// In the water with the board on the feet and the kite at 12.
		Board->Velocity = FVector::ZeroVector;
		Board->SetBoardState(EBoardState::Displacement);
		Kite->SetWindowPosition(0.0f, LessonDirectorPrivate::ZenithStartDepthDeg);
	}

	// The lesson's wind, kite and board, on the pawn's own components. InitializeRide has just
	// applied the player's gear from the game instance; the lesson's replaces it for this run only.
	if (UWindComponent* Wind = P->GetWind())
	{
		const FVector Direction = Wind->BaseWind.IsNearlyZero() ? FVector::ForwardVector : Wind->BaseWind.GetSafeNormal();
		Wind->BaseWind = Direction * KiteUnits::KnotsToCmS(WindKnots);
	}
	Kite->SetKiteModel(Setup.Kite);
	Kite->SetKiteSize(Setup.KiteSizeM2 > 0.0f ? Setup.KiteSizeM2 : UKiteComponent::RecommendKiteSizeM2(WindKnots));
	Board->SetBoardSize(Setup.Board);
	ApplyAssists(UsedAssists);
}

void ALessonDirector::ApplyAssists(const FLessonAssists& Assists)
{
	AKiteRiderPawn* P = Rider.Get();
	if (!P)
	{
		return;
	}
	if (UKiteComponent* Kite = P->GetKite())
	{
		Kite->bParkHoldAssist = Assists.bAutoPark;
	}
	if (UBoardMovementComponent* Board = P->GetBoardMovement())
	{
		Board->bAutoEdge = Assists.bAutoEdge;
	}
	if (URiderAttitudeComponent* Attitude = P->GetRiderAttitude())
	{
		Attitude->AssistStrength = Assists.bLandingAssist ? 1.0f : 0.0f;
	}
	// Auto-redirect (physics phase 3), loop catch (chapter E) and slow motion (S8) do not exist
	// yet: they count towards the stars as the lesson sets them but change nothing on the rider.
}

float ALessonDirector::BoardTime() const
{
	const AKiteRiderPawn* P = Rider.Get();
	const UBoardMovementComponent* Board = P ? P->GetBoardMovement() : nullptr;
	return Board ? Board->GetSimTimeSeconds() : 0.0f;
}

FLessonSample ALessonDirector::MakeSample() const
{
	FLessonSample S;
	const AKiteRiderPawn* P = Rider.Get();
	const UBoardMovementComponent* Board = P ? P->GetBoardMovement() : nullptr;
	const UKiteComponent* Kite = P ? P->GetKite() : nullptr;
	if (!Board || !Kite)
	{
		return S;
	}
	S.TimeSeconds = Board->GetSimTimeSeconds();
	S.BoardState = Board->GetBoardState();

	const FVector Velocity = Board->Velocity;
	const FVector Velocity2D(Velocity.X, Velocity.Y, 0.0f);
	S.SpeedMS = KiteUnits::CmToM(Velocity2D.Size());
	const FVector Travel = S.SpeedMS > LessonDirectorPrivate::MinTackSpeedMS ? Velocity2D.GetSafeNormal() : P->GetActorForwardVector().GetSafeNormal2D();
	S.HeadingDeg = FMath::RadiansToDegrees(FMath::Atan2(Travel.Y, Travel.X));

	const FVector Downwind = Kite->GetDownwindDir().GetSafeNormal2D();
	const FVector Crosswind = FVector::CrossProduct(FVector::UpVector, Downwind);
	// +1 riding to the right looking downwind, as AKiteSurfGameMode::InitializeRide's TackSide.
	S.Tack = S.SpeedMS > LessonDirectorPrivate::MinTackSpeedMS ? (FVector::DotProduct(Velocity2D, Crosswind) >= 0.0f ? 1 : -1) : 0;
	S.EdgeInput = Board->GetEdgeInput();

	const FVector RiderPos = P->GetActorLocation();
	S.HeightM = FMath::Max(KiteUnits::CmToM(RiderPos.Z - Board->GetWaterSurfaceHeightCm()), 0.0f);
	S.VerticalSpeedMS = KiteUnits::CmToM(Velocity.Z);

	S.KiteClockDeg = Kite->GetClockDeg();
	S.KiteElevationDeg = Kite->GetElevationDeg();
	const FVector ToKite = Kite->GetKiteWorldPosition() - RiderPos;
	S.KiteDownwindM = KiteUnits::CmToM(FVector::DotProduct(ToKite, Downwind));
	const FVector ToKite2D = FVector(ToKite.X, ToKite.Y, 0.0f).GetSafeNormal();
	S.KiteBearingDeg = ToKite2D.IsNearlyZero() ? 0.0f
		: FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Travel, ToKite2D), -1.0f, 1.0f)));
	S.TensionN = Kite->GetLineTensionN();
	S.BarPosition = P->GetCurrentSheetInput();
	S.SteerInput = P->GetCurrentSteerInput();
	S.UpwindM = -KiteUnits::CmToM(FVector::DotProduct(RiderPos, Downwind));
	S.CompletedLoops = CompletedLoops;
	S.bFallen = Board->IsCrashing() || Kite->IsCrashed();
	S.bToeside = false; // toeside riding does not exist yet (tricks T3.7)
	return S;
}

bool ALessonDirector::SampleTelemetry()
{
	const AKiteRiderPawn* P = Rider.Get();
	const UKiteComponent* Kite = P ? P->GetKite() : nullptr;
	if (!Kite || !P->GetBoardMovement())
	{
		return false;
	}
	// Completed loops: a running count from the kite's own loop records.
	const int32 TotalLoops = Kite->GetLoopRecordCount();
	if (TotalLoops > SeenLoopRecords)
	{
		const TArray<FKiteLoopRecord>& Records = Kite->GetLoopRecords();
		const int32 NewRecords = FMath::Min(TotalLoops - SeenLoopRecords, Records.Num());
		for (int32 I = Records.Num() - NewRecords; I < Records.Num(); ++I)
		{
			CompletedLoops += Records[I].bCompleted ? 1 : 0;
		}
		SeenLoopRecords = TotalLoops;
	}

	// One sample per StepsPerSample fixed steps on average. Frames do not always hold a whole number
	// of those (at 60 fps the pawn steps 3, 5, 4... times), so the next sample is due on a fixed
	// schedule and taken by the first update within 1.5 steps of it: no frame is skipped at 60 fps,
	// and faster frame rates still sample at 60 Hz.
	const float Step = FMath::Max(P->SimStepSeconds, KINDA_SMALL_NUMBER);
	const float Interval = LessonDirectorPrivate::StepsPerSample * Step;
	const float Now = BoardTime();
	if (Now < NextSampleTime - 1.5f * Step)
	{
		return false;
	}
	if (!Telemetry.Add(MakeSample()))
	{
		return false;
	}
	// After a long gap (a hitch, the intro of a test with no frames) the schedule restarts from now.
	NextSampleTime = FMath::Max(NextSampleTime + Interval, Now + 0.5f * Interval);
	return true;
}

const FJumpRecord* ALessonDirector::NewestJump(FLessonJumpExtras& OutExtras)
{
	FJumpRecord& Newest = NewestJumpRecord;
	OutExtras = FLessonJumpExtras();
	const AKiteRiderPawn* P = Rider.Get();
	const UTrickTrackerComponent* Tracker = P ? P->GetTrickTracker() : nullptr;
	if (!Tracker || !Tracker->GetLastJumpRecord(Newest))
	{
		return nullptr;
	}
	// The landing evaluator's cause and the rider's rotation are on the record (T1.6); grabs are
	// not known yet (tricks T2.1), so their metrics stay unavailable.
	OutExtras.LandingCause = Newest.LandingCause;
	if (Newest.bRotationTracked)
	{
		OutExtras.bHasRotation = true;
		// As LessonEval::ExtrasFromSignature: 360 per inversion plus 180 per spin half turn.
		OutExtras.RotationDeg = 360.0f * Newest.Inversions.Num() + 180.0f * Newest.SpinHalfTurns;
	}
	return &Newest;
}

void ALessonDirector::UpdateLesson(float DeltaSeconds)
{
	if (Phase == ELessonPhase::Idle)
	{
		return;
	}
	if (!Rider.IsValid())
	{
		UE_LOG(LogKiteSchool, Warning, TEXT("Lesson %s: the rider is gone; lesson ended"), *Lesson.Id.ToString());
		SetPhase(ELessonPhase::Idle);
		return;
	}
	const bool bNewSample = SampleTelemetry();
	PhaseSeconds += FMath::Max(DeltaSeconds, 0.0f);

	switch (Phase)
	{
	case ELessonPhase::Intro:
		if (PhaseSeconds >= IntroSeconds && !Telemetry.IsEmpty())
		{
			LessonStartTime = BoardTime();
			EnterStep(0);
		}
		break;

	case ELessonPhase::Step:
		if (bNewSample)
		{
			GradeSheetIn();
			JudgeStep();
		}
		break;

	case ELessonPhase::Result:
		if (Outcome == ELessonOutcome::AttemptFailed && PhaseSeconds >= AttemptResultSeconds)
		{
			// Back to the step with its progress: anything that happened while the line showed is
			// judged now (the evaluators find events newer than the last one judged).
			SetPhase(ELessonPhase::Step);
			if (bNewSample)
			{
				JudgeStep();
			}
		}
		break;

	default:
		break;
	}

	const bool bJudging = Phase == ELessonPhase::Step || (Phase == ELessonPhase::Result && Outcome == ELessonOutcome::AttemptFailed);
	if (bJudging && LessonTimeLimitSeconds > 0.0f && BoardTime() - LessonStartTime >= LessonTimeLimitSeconds)
	{
		UE_LOG(LogKiteSchool, Display, TEXT("Lesson %s: time limit (%.0f s) reached"), *Lesson.Id.ToString(), LessonTimeLimitSeconds);
		if (ULessonSubsystem* Lessons = GetLessons())
		{
			Lessons->RecordLessonResult(Lesson.Id, false, 0, std::numeric_limits<float>::quiet_NaN(), UsedAssists.CountOn() == 0);
		}
		bRecorded = true;
		EnterResult(ELessonOutcome::Failed);
	}
}

const FLessonObjective* ALessonDirector::GetCurrentObjective() const
{
	if (Phase == ELessonPhase::Idle || !Lesson.Steps.IsValidIndex(StepIndex))
	{
		return nullptr;
	}
	// The last step is the lesson's test: it is judged by the pass objective (in the catalogue the
	// two are the same; a lesson whose test differs shows the last step's prompt with the test).
	return IsLastStep() ? &Lesson.Pass : &Lesson.Steps[StepIndex].Objective;
}

void ALessonDirector::JudgeStep()
{
	const FLessonObjective* Objective = GetCurrentObjective();
	if (!Objective)
	{
		return;
	}
	FLessonJumpExtras Extras;
	const FJumpRecord* Jump = NewestJump(Extras);
	const AKiteRiderPawn* P = Rider.Get();
	const UTrickTrackerComponent* Tracker = P ? P->GetTrickTracker() : nullptr;
	const int32 RecordCount = Tracker ? Tracker->GetJumpRecordCount() : 0;
	if (RecordCount != LastSeenRecordCount)
	{
		LastSeenRecordCount = RecordCount;
		if (Jump)
		{
			UE_LOG(LogKiteSchool, Display, TEXT("Lesson %s: jump %d finished: %.2f m, %s, %s, cause %s"),
				*Lesson.Id.ToString(), Jump->Index, KiteUnits::CmToM(Jump->ApexHeightCm),
				Jump->bPopped ? TEXT("popped") : TEXT("lifted"), *UEnum::GetValueAsString(Jump->Grade), *UEnum::GetValueAsString(Extras.LandingCause));
		}
	}

	const FObjectiveResult Result = LessonEval::EvaluateObjective(*Objective, StepProgress, Telemetry, Jump, Extras);
	const int32 AttemptsBefore = StepProgress.Attempts;
	const int32 CountBefore = StepProgress.Count;
	LessonEval::ApplyResult(StepProgress, Result);
	LessonAttempts += FMath::Max(StepProgress.Attempts - AttemptsBefore, 0);
	LastResult = Result;

	bool bHigherNow = false;
	if (IsLastStep() && Lesson.Stars.HigherBar.Num() > 0)
	{
		const FObjectiveResult Higher = LessonEval::EvaluateObjective(LessonEval::HigherBarObjective(Lesson), HigherBarProgress, Telemetry, Jump, Extras);
		LessonEval::ApplyResult(HigherBarProgress, Higher);
		bHigherNow = Higher.bPassed;
	}

	// A timing-ring step grades the moment of each judged attempt (a sheet-in step grades it live instead).
	if (StepProgress.Attempts != AttemptsBefore && Lesson.Steps.IsValidIndex(StepIndex))
	{
		const FLessonStep& Step = Lesson.Steps[StepIndex];
		if (Step.Cue == ELessonCue::TimingRing && !LessonTiming::IsSheetInStep(Step))
		{
			const ELessonTimingGrade Grade = LessonTiming::GradeAttempt(Step, *Objective, Telemetry, Jump, Extras);
			if (Grade != ELessonTimingGrade::None)
			{
				SetTimingGrade(Grade);
			}
		}
	}

	if (StepProgress.Count != CountBefore)
	{
		UE_LOG(LogKiteSchool, Display, TEXT("Lesson %s step %d: counted %d of %d (value %.2f)"),
			*Lesson.Id.ToString(), StepIndex + 1, StepProgress.Count, FMath::Max(Objective->Count, 1), Result.Value);
	}

	if (Result.bPassed)
	{
		if (IsLastStep())
		{
			bHigherBarMet = bHigherNow;
			OnPassed(Result.Value);
		}
		else
		{
			EnterStep(StepIndex + 1);
		}
	}
	else if (Result.bFailed)
	{
		OnAttemptFailed(LessonDirectorPrivate::NeedsJump(Objective->PrimaryMeasure()) ? Jump : nullptr, Extras);
	}
}

void ALessonDirector::SetTimingGrade(ELessonTimingGrade Grade)
{
	TimingGrade = Grade;
	++TimingSerial;
	UE_LOG(LogKiteSchool, Display, TEXT("Lesson %s step %d: timing %s"), *Lesson.Id.ToString(), StepIndex + 1, *LessonTiming::GradeText(Grade));
}

void ALessonDirector::GradeSheetIn()
{
	if (!Lesson.Steps.IsValidIndex(StepIndex))
	{
		return;
	}
	const FLessonStep& Step = Lesson.Steps[StepIndex];
	if (Step.Cue != ELessonCue::TimingRing || !LessonTiming::IsSheetInStep(Step) || Telemetry.IsEmpty())
	{
		return;
	}
	const float Now = Telemetry.LatestTime();
	ELessonTimingGrade Grade = ELessonTimingGrade::None;
	if (Now - LastSheetGradeTime >= LessonTiming::SheetRegradeSeconds && LessonTiming::DetectSheetIn(Telemetry, Grade))
	{
		LastSheetGradeTime = Now;
		SetTimingGrade(Grade);
	}
}

void ALessonDirector::OnAttemptFailed(const FJumpRecord* Jump, const FLessonJumpExtras& Extras)
{
	++StepFailures;
	const FLessonFault* Fault = nullptr;
	if (Jump)
	{
		Fault = LessonEval::DiagnoseFault(Lesson.Faults, Telemetry, *Jump, Extras);
	}
	else
	{
		// A ride event missed: only the rules that read the telemetry alone can say why.
		TArray<FLessonFault> RideFaults;
		for (const FLessonFault& F : Lesson.Faults)
		{
			if (!LessonDirectorPrivate::NeedsJump(F.Measure))
			{
				RideFaults.Add(F);
			}
		}
		const FLessonFault* Found = LessonEval::DiagnoseFault(RideFaults, Telemetry, FJumpRecord(), FLessonJumpExtras());
		if (Found)
		{
			Fault = Lesson.Faults.FindByPredicate([Found](const FLessonFault& F) { return F.Id == Found->Id; });
		}
	}
	LastFaultLine = Fault ? Fault->Feedback : FText::GetEmpty();
	LastFaultId = Fault ? Fault->Id : NAME_None;
	if (StepFailures >= DropBackAfterFailures && !bOfferDropBack)
	{
		bOfferDropBack = true;
		UE_LOG(LogKiteSchool, Display, TEXT("Lesson %s step %d: %d failed attempts, offering to drop back"), *Lesson.Id.ToString(), StepIndex + 1, StepFailures);
	}
	UE_LOG(LogKiteSchool, Display, TEXT("Lesson %s step %d: attempt failed (%d on this step): %s \"%s\""),
		*Lesson.Id.ToString(), StepIndex + 1, StepFailures, *LastFaultId.ToString(), *LastFaultLine.ToString());
	EnterResult(ELessonOutcome::AttemptFailed);
}

void ALessonDirector::OnPassed(float Value)
{
	bPassedHigherBar = bHigherBarMet;
	Stars = LessonEval::ComputeStars(Lesson.Stars, Lesson.Setup.Assists, UsedAssists, true, bPassedHigherBar);
	const bool bNoAssists = UsedAssists.CountOn() == 0;
	if (ULessonSubsystem* Lessons = GetLessons())
	{
		Lessons->RecordLessonResult(Lesson.Id, true, Stars, Value, bNoAssists);
	}
	else
	{
		UE_LOG(LogKiteSchool, Warning, TEXT("Lesson %s: no lesson subsystem; the pass is not recorded"), *Lesson.Id.ToString());
	}
	bRecorded = true;
	UE_LOG(LogKiteSchool, Display, TEXT("Lesson %s: passed with %d star(s)%s, value %.2f"),
		*Lesson.Id.ToString(), Stars, bPassedHigherBar ? TEXT(" (higher bar met)") : TEXT(""), Value);
	EnterResult(ELessonOutcome::Passed);
}

void ALessonDirector::RecordAbandonedAttempt()
{
	if (Phase == ELessonPhase::Idle || bRecorded || LessonAttempts == 0)
	{
		return;
	}
	if (ULessonSubsystem* Lessons = GetLessons())
	{
		Lessons->RecordLessonResult(Lesson.Id, false, 0, std::numeric_limits<float>::quiet_NaN(), UsedAssists.CountOn() == 0);
	}
	bRecorded = true;
}

void ALessonDirector::EnterIntro()
{
	Outcome = ELessonOutcome::None;
	SetPhase(ELessonPhase::Intro);
}

void ALessonDirector::EnterStep(int32 Index)
{
	StepIndex = FMath::Clamp(Index, 0, FMath::Max(Lesson.Steps.Num() - 1, 0));
	const AKiteRiderPawn* P = Rider.Get();
	const UTrickTrackerComponent* Tracker = P ? P->GetTrickTracker() : nullptr;
	FJumpRecord Last;
	const int32 LastJumpIndex = Tracker && Tracker->GetLastJumpRecord(Last) ? Last.Index : -1;
	StepProgress = LessonEval::BeginStep(Telemetry, LastJumpIndex);
	HigherBarProgress = StepProgress;
	bHigherBarMet = false;
	LastResult = FObjectiveResult();
	StepFailures = 0;
	bOfferDropBack = false;
	Outcome = ELessonOutcome::None;
	SetPhase(ELessonPhase::Step);
}

void ALessonDirector::EnterResult(ELessonOutcome InOutcome)
{
	Outcome = InOutcome;
	SetPhase(ELessonPhase::Result);
}

void ALessonDirector::SetPhase(ELessonPhase InPhase)
{
	const ELessonPhase Old = Phase;
	Phase = InPhase;
	PhaseSeconds = 0.0f;
	if (InPhase == ELessonPhase::Step && Lesson.Steps.IsValidIndex(StepIndex))
	{
		UE_LOG(LogKiteSchool, Display, TEXT("Lesson %s: %s -> Step %d of %d: \"%s\" [%s]"),
			*Lesson.Id.ToString(), PhaseName(Old), StepIndex + 1, Lesson.Steps.Num(),
			*Lesson.Steps[StepIndex].Prompt.ToString(), *Lesson.Steps[StepIndex].InputGlyph.ToString());
	}
	else
	{
		UE_LOG(LogKiteSchool, Display, TEXT("Lesson %s: %s -> %s%s%s"), *Lesson.Id.ToString(), PhaseName(Old), PhaseName(InPhase),
			InPhase == ELessonPhase::Result ? TEXT(" ") : TEXT(""), InPhase == ELessonPhase::Result ? OutcomeName(Outcome) : TEXT(""));
	}
}

bool ALessonDirector::Retry()
{
	if (Phase == ELessonPhase::Idle || !Rider.IsValid())
	{
		return false;
	}
	return BeginLesson(Lesson, Rider.Get(), RunOptions);
}

FName ALessonDirector::GetNextLessonId() const
{
	if (Phase == ELessonPhase::Idle)
	{
		return NAME_None;
	}
	const TArray<FLessonDef>& All = LessonCatalog::GetAll();
	const int32 Here = All.IndexOfByPredicate([this](const FLessonDef& L) { return L.Id == Lesson.Id; });
	const ULessonSubsystem* Lessons = GetLessons();
	for (int32 I = Here + 1; Here != INDEX_NONE && I < All.Num(); ++I)
	{
		const bool bOpen = Lessons ? Lessons->IsUnlocked(All[I].Id) : LessonCatalog::IsAvailable(All[I]);
		if (bOpen)
		{
			return All[I].Id;
		}
	}
	return NAME_None;
}

bool ALessonDirector::Next()
{
	if (Phase != ELessonPhase::Result || Outcome != ELessonOutcome::Passed)
	{
		return false;
	}
	const FLessonDef* NextLesson = LessonCatalog::Find(GetNextLessonId());
	if (!NextLesson)
	{
		return false;
	}
	// Every lesson rides L_FlatWater, the level the rider is already in: the next one starts in place.
	return BeginLesson(*NextLesson, Rider.Get(), FLessonRunOptions());
}

FName ALessonDirector::GetDropBackLessonId() const
{
	return Phase != ELessonPhase::Idle && Lesson.Requires.Num() > 0 ? Lesson.Requires[0] : NAME_None;
}

bool ALessonDirector::AcceptDropBack()
{
	if (!bOfferDropBack || Phase == ELessonPhase::Idle)
	{
		return false;
	}
	if (StepIndex > 0)
	{
		UE_LOG(LogKiteSchool, Display, TEXT("Lesson %s: dropping back to step %d"), *Lesson.Id.ToString(), StepIndex);
		EnterStep(StepIndex - 1);
		return true;
	}
	const FLessonDef* Previous = LessonCatalog::Find(GetDropBackLessonId());
	if (!Previous)
	{
		return false;
	}
	UE_LOG(LogKiteSchool, Display, TEXT("Lesson %s: dropping back to lesson %s"), *Lesson.Id.ToString(), *Previous->Id.ToString());
	return BeginLesson(*Previous, Rider.Get(), FLessonRunOptions());
}

void ALessonDirector::ExitToFreeRide()
{
	if (Phase != ELessonPhase::Idle)
	{
		UE_LOG(LogKiteSchool, Display, TEXT("Lesson %s: exit to free ride"), *Lesson.Id.ToString());
		RecordAbandonedAttempt();
		RestoreSnapshot();
		SetPhase(ELessonPhase::Idle);
	}
	Snapshot.bValid = false;
	Destroy();
}

ULessonSubsystem* ALessonDirector::GetLessons() const
{
	if (LessonsOverride)
	{
		return LessonsOverride.Get();
	}
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<ULessonSubsystem>() : nullptr;
}

ALessonDirector* ALessonDirector::StartInWorld(UWorld* World, const FLessonDef& InLesson, APawn* Pawn, const FLessonRunOptions& Options)
{
	if (!World)
	{
		return nullptr;
	}
	if (!Pawn)
	{
		const APlayerController* PC = World->GetFirstPlayerController();
		Pawn = PC ? PC->GetPawn() : nullptr;
	}
	if (!Cast<AKiteRiderPawn>(Pawn))
	{
		UE_LOG(LogKiteSchool, Warning, TEXT("Lesson %s: no kite rider in %s to run it on"), *InLesson.Id.ToString(), *World->GetName());
		return nullptr;
	}
	// One lesson at a time: a running one ends (and puts the rider's gear back) first.
	TArray<ALessonDirector*> Existing;
	for (TActorIterator<ALessonDirector> It(World); It; ++It)
	{
		Existing.Add(*It);
	}
	for (ALessonDirector* Old : Existing)
	{
		Old->ExitToFreeRide();
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ALessonDirector* Director = World->SpawnActor<ALessonDirector>(Params);
	if (Director && !Director->BeginLesson(InLesson, Pawn, Options))
	{
		Director->Destroy();
		return nullptr;
	}
	return Director;
}

FText ALessonDirector::GetPrompt() const
{
	switch (Phase)
	{
	case ELessonPhase::Intro:
		return Lesson.Summary.IsEmpty() ? Lesson.Title : Lesson.Summary;
	case ELessonPhase::Step:
		return Lesson.Steps.IsValidIndex(StepIndex) ? Lesson.Steps[StepIndex].Prompt : FText::GetEmpty();
	case ELessonPhase::Result:
		if (Outcome == ELessonOutcome::AttemptFailed)
		{
			// The fault line replaces the prompt while it shows; with no rule matched, the step's prompt again.
			return !LastFaultLine.IsEmpty() ? LastFaultLine
				: (Lesson.Steps.IsValidIndex(StepIndex) ? Lesson.Steps[StepIndex].Prompt : FText::GetEmpty());
		}
		return FText::GetEmpty();
	default:
		return FText::GetEmpty();
	}
}

FName ALessonDirector::GetInputGlyph() const
{
	const bool bInStep = Phase == ELessonPhase::Step || (Phase == ELessonPhase::Result && Outcome == ELessonOutcome::AttemptFailed);
	return bInStep && Lesson.Steps.IsValidIndex(StepIndex) ? Lesson.Steps[StepIndex].InputGlyph : NAME_None;
}

ELessonCue ALessonDirector::GetCue() const
{
	const bool bInStep = Phase == ELessonPhase::Step || (Phase == ELessonPhase::Result && Outcome == ELessonOutcome::AttemptFailed);
	return bInStep && Lesson.Steps.IsValidIndex(StepIndex) ? Lesson.Steps[StepIndex].Cue : ELessonCue::None;
}

const TCHAR* ALessonDirector::PhaseName(ELessonPhase InPhase)
{
	switch (InPhase)
	{
	case ELessonPhase::Idle:   return TEXT("Idle");
	case ELessonPhase::Intro:  return TEXT("Intro");
	case ELessonPhase::Step:   return TEXT("Step");
	case ELessonPhase::Result: return TEXT("Result");
	default:                   return TEXT("?");
	}
}

const TCHAR* ALessonDirector::OutcomeName(ELessonOutcome InOutcome)
{
	switch (InOutcome)
	{
	case ELessonOutcome::None:          return TEXT("None");
	case ELessonOutcome::AttemptFailed: return TEXT("AttemptFailed");
	case ELessonOutcome::Failed:        return TEXT("Failed");
	case ELessonOutcome::Passed:        return TEXT("Passed");
	default:                            return TEXT("?");
	}
}
