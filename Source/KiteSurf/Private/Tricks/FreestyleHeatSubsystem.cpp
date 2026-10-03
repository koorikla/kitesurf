#include "Tricks/FreestyleHeatSubsystem.h"
#include "BoardMovementComponent.h"
#include "KiteRiderPawn.h"
#include "KiteSurf.h"
#include "School/LessonDirector.h"
#include "Tricks/TrickSessionSubsystem.h"
#include "Tricks/TrickTrackerComponent.h"
#include "UI/KiteSurfGameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"

const TCHAR* UFreestyleHeatSubsystem::UnhookNotice = TEXT("Unhook for freestyle");

bool UFreestyleHeatSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UFreestyleHeatSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFreestyleHeatSubsystem, STATGROUP_Tickables);
}

bool UFreestyleHeatSubsystem::IsTickable() const
{
	return Heat.IsActive() || IsShowingResults();
}

void UFreestyleHeatSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	StepHeat(DeltaTime);
}

void UFreestyleHeatSubsystem::PostNotice(const FString& Text)
{
	Notice = Text;
	++NoticeSerial;
}

EHeatStartResult UFreestyleHeatSubsystem::FindBlockingMode() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return EHeatStartResult::Started;
	}
	if (const UTrickSessionSubsystem* Sessions = World->GetSubsystem<UTrickSessionSubsystem>())
	{
		if (Sessions->IsSessionActive())
		{
			return EHeatStartResult::SessionActive;
		}
	}
	for (TActorIterator<ALessonDirector> It(World); It; ++It)
	{
		if (IsValid(*It) && It->IsRunning())
		{
			return EHeatStartResult::LessonActive;
		}
	}
	return EHeatStartResult::Started;
}

EHeatStartResult UFreestyleHeatSubsystem::StartHeat(int32 Attempts, UTrickTrackerComponent* InTracker, float CountdownSeconds)
{
	const EHeatStartResult Blocking = FindBlockingMode();
	if (Blocking != EHeatStartResult::Started)
	{
		PostNotice(Blocking == EHeatStartResult::SessionActive ? TEXT("Finish the session before a freestyle heat")
			: TEXT("Leave the lesson before a freestyle heat"));
		UE_LOG(LogKiteSurf, Log, TEXT("Freestyle heat: not started, %s"), *UEnum::GetDisplayValueAsText(Blocking).ToString());
		return Blocking;
	}

	if (!InTracker)
	{
		// The player's rider, or any rider when nobody controls one.
		AKiteRiderPawn* Fallback = nullptr;
		for (TActorIterator<AKiteRiderPawn> It(GetWorld()); It; ++It)
		{
			if (It->IsPlayerControlled())
			{
				InTracker = It->GetTrickTracker();
				break;
			}
			Fallback = Fallback ? Fallback : *It;
		}
		if (!InTracker && Fallback)
		{
			InTracker = Fallback->GetTrickTracker();
		}
	}
	if (!InTracker)
	{
		UE_LOG(LogKiteSurf, Log, TEXT("Freestyle heat: no rider with a trick tracker to follow"));
		return EHeatStartResult::NoRider;
	}

	Tracker = InTracker;
	AActor* Owner = InTracker->GetOwner();
	Board = Owner ? Owner->FindComponentByClass<UBoardMovementComponent>() : nullptr;
	SeenRecordCount = InTracker->GetJumpRecordCount();
	LastClockSeconds = Board.IsValid() ? Board->GetSimTimeSeconds() : LastClockSeconds;
	bNewBest = false;
	// Score with the signatures the tracker named the jumps with.
	const FJumpRecorderSettings& Recorder = InTracker->GetJumpSession().GetRecorder().Settings;
	Heat.LoopClassify = Recorder.LoopClassify;
	Heat.LandingGrade = Recorder.LandingGrade;
	Heat.Settings.Scoring = Recorder.Scoring;
	const float SavedCountdown = Heat.Settings.TrickCountdownSeconds;
	if (CountdownSeconds >= 0.0f)
	{
		Heat.Settings.TrickCountdownSeconds = CountdownSeconds;
	}
	Heat.Start(Attempts, LastClockSeconds);
	Heat.Settings.TrickCountdownSeconds = SavedCountdown;
	ReadLocalBest();

	const AKiteRiderPawn* Rider = Cast<AKiteRiderPawn>(Owner);
	PostNotice(Rider && Rider->IsHooked() ? FString(UnhookNotice)
		: FString::Printf(TEXT("Freestyle heat: %d tricks"), Heat.GetAttemptLimit()));
	UE_LOG(LogKiteSurf, Log, TEXT("Freestyle heat: %d attempts started, countdown %s (local best %.1f)"), Heat.GetAttemptLimit(),
		Heat.IsCountdownOn() ? *FString::Printf(TEXT("%.0f s"), Heat.GetCountdownLeft()) : TEXT("off"), PreviousBest);
	return EHeatStartResult::Started;
}

void UFreestyleHeatSubsystem::CancelHeat(const FString& InNotice)
{
	if (!Heat.IsActive())
	{
		return;
	}
	Heat.Cancel();
	if (!InNotice.IsEmpty())
	{
		PostNotice(InNotice);
	}
	UE_LOG(LogKiteSurf, Log, TEXT("Freestyle heat: cancelled%s%s"), InNotice.IsEmpty() ? TEXT("") : TEXT(", "), *InNotice);
}

void UFreestyleHeatSubsystem::ReadLocalBest()
{
	PreviousBest = 0.0f;
	bHadPreviousBest = false;
	const UWorld* World = GetWorld();
	if (const UKiteSurfGameInstance* GameInstance = World ? World->GetGameInstance<UKiteSurfGameInstance>() : nullptr)
	{
		bHadPreviousBest = GameInstance->HasBestHeatTotal(Heat.GetAttemptLimit());
		PreviousBest = GameInstance->GetBestHeatTotal(Heat.GetAttemptLimit());
	}
}

void UFreestyleHeatSubsystem::StepHeat(float FallbackDeltaSeconds)
{
	if (Heat.GetPhase() == EFreestyleHeatPhase::Idle)
	{
		return;
	}

	if (Heat.IsActive() && FindBlockingMode() != EHeatStartResult::Started)
	{
		CancelHeat(TEXT("Freestyle heat cancelled"));
		return;
	}

	const bool bWasFinished = Heat.IsFinished();
	UTrickTrackerComponent* Source = Tracker.Get();
	if (Source && !bWasFinished)
	{
		// New records since the last step, oldest first. A cleared tracker restarts the count.
		const int32 Count = Source->GetJumpRecordCount();
		if (Count < SeenRecordCount)
		{
			SeenRecordCount = Count;
		}
		const TArray<FJumpRecord>& Records = Source->GetJumpRecords();
		const int32 NewRecords = FMath::Min(Count - SeenRecordCount, Records.Num());
		for (int32 Index = Records.Num() - NewRecords; Index < Records.Num() && Heat.IsActive(); ++Index)
		{
			const FJumpRecord& Record = Records[Index];
			const int32 Before = Heat.GetAttemptCount();
			const EHeatRecordVerdict Verdict = Heat.OfferRecord(Record);
			if (Verdict == EHeatRecordVerdict::Attempt || Verdict == EHeatRecordVerdict::Crash)
			{
				const FHeatAttempt& Taken = Heat.GetAttempts()[Before];
				UE_LOG(LogKiteSurf, Log, TEXT("Freestyle heat: trick %d/%d %s (%s) %.1f m, %s %.1f pts; total %.1f (bonus %.0f)"), Before + 1,
					Heat.GetAttemptLimit(), *Taken.Trick.Name, *FreestyleHeat::FamilyLabel(Taken.Trick.Family), Taken.ApexM,
					*UEnum::GetDisplayValueAsText(Taken.Kind).ToString(), Taken.Trick.Score, Heat.GetTotal(), Heat.GetResult().VarietyBonus);
			}
			else if (Verdict == EHeatRecordVerdict::Hooked && Record.AirtimeSeconds > Heat.Settings.MinAirtimeSeconds)
			{
				// A real hooked jump, not a bump on the chop.
				PostNotice(UnhookNotice);
			}
		}
		SeenRecordCount = Count;
	}

	const float Clock = Board.IsValid() ? Board->GetSimTimeSeconds() : LastClockSeconds + FMath::Max(FallbackDeltaSeconds, 0.0f);
	const float Delta = FMath::Max(Clock - LastClockSeconds, 0.0f);
	LastClockSeconds = Clock;
	const FJumpRecord* JumpInAir = (Source && Source->IsJumpInProgress()) ? &Source->GetLiveJump() : nullptr;
	const int32 BeforeTick = Heat.GetAttemptCount();
	Heat.Tick(Delta, JumpInAir);
	if (Heat.GetAttemptCount() > BeforeTick)
	{
		PostNotice(FString::Printf(TEXT("Trick %d lost: time ran out"), Heat.GetAttemptCount()));
		UE_LOG(LogKiteSurf, Log, TEXT("Freestyle heat: trick %d/%d lost to the countdown"), Heat.GetAttemptCount(), Heat.GetAttemptLimit());
	}

	if (!bWasFinished && Heat.IsFinished())
	{
		OnFinished();
	}
}

void UFreestyleHeatSubsystem::OnFinished()
{
	const float Total = Heat.GetTotal();
	const int32 Key = Heat.GetAttemptLimit();
	ReadLocalBest();
	bNewBest = Total > 0.0f && (!bHadPreviousBest || Total > PreviousBest);

	const UWorld* World = GetWorld();
	if (UKiteSurfGameInstance* GameInstance = World ? World->GetGameInstance<UKiteSurfGameInstance>() : nullptr)
	{
		if (GameInstance->RecordHeatTotal(Key, Total))
		{
			// The settings save path: it also carries the trick book and the session bests.
			GameInstance->SaveSettingsToDisk();
		}
	}
	UE_LOG(LogKiteSurf, Log, TEXT("Freestyle heat: %d tricks over, total %.1f (tricks %.1f + bonus %.0f, %d families)%s (local best before %.1f)"), Key,
		Total, Heat.GetResult().TrickTotal, Heat.GetResult().VarietyBonus, Heat.GetResult().CountingIdx.Num(), bNewBest ? TEXT(", NEW BEST") : TEXT(""),
		PreviousBest);
}
