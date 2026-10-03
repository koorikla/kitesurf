#include "Tricks/TrickSessionSubsystem.h"
#include "BoardMovementComponent.h"
#include "KiteRiderPawn.h"
#include "KiteSurf.h"
#include "KiteSurfUnits.h"
#include "Tricks/TrickTrackerComponent.h"
#include "UI/KiteSurfGameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"

bool UTrickSessionSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UTrickSessionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTrickSessionSubsystem, STATGROUP_Tickables);
}

bool UTrickSessionSubsystem::IsTickable() const
{
	return Session.IsActive() || IsShowingResults();
}

void UTrickSessionSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	StepSession(DeltaTime);
}

bool UTrickSessionSubsystem::StartSession(float DurationSeconds, UTrickTrackerComponent* InTracker)
{
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
		UE_LOG(LogKiteSurf, Log, TEXT("Best-three session: no rider with a trick tracker to follow"));
		return false;
	}

	Tracker = InTracker;
	Board = InTracker->GetOwner() ? InTracker->GetOwner()->FindComponentByClass<UBoardMovementComponent>() : nullptr;
	SeenRecordCount = InTracker->GetJumpRecordCount();
	LastClockSeconds = ReadClock();
	bNewBest = false;
	Session.Start(DurationSeconds, LastClockSeconds);
	ReadLocalBest();
	UE_LOG(LogKiteSurf, Log, TEXT("Best-three session: %.1f s started (local best %.1f)"), Session.GetDuration(), PreviousBest);
	return true;
}

float UTrickSessionSubsystem::ReadClock() const
{
	return Board.IsValid() ? Board->GetSimTimeSeconds() : LastClockSeconds;
}

void UTrickSessionSubsystem::ReadLocalBest()
{
	PreviousBest = 0.0f;
	bHadPreviousBest = false;
	const UWorld* World = GetWorld();
	if (const UKiteSurfGameInstance* GameInstance = World ? World->GetGameInstance<UKiteSurfGameInstance>() : nullptr)
	{
		bHadPreviousBest = GameInstance->HasBestSessionTotal(GetDurationKey());
		PreviousBest = GameInstance->GetBestSessionTotal(GetDurationKey());
	}
}

void UTrickSessionSubsystem::StepSession(float FallbackDeltaSeconds)
{
	if (Session.GetPhase() == EBestThreePhase::Idle)
	{
		return;
	}

	const bool bWasFinished = Session.IsFinished();
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
		for (int32 Index = Records.Num() - NewRecords; Index < Records.Num(); ++Index)
		{
			if (Session.AddJump(Records[Index]))
			{
				const FJumpRecord& Taken = Session.GetJumps().Last();
				UE_LOG(LogKiteSurf, Log, TEXT("Best-three session: %s %.1f m, %.1f pts (x%.2f), total %.1f"), *Taken.TrickName,
					KiteUnits::CmToM(Taken.ApexHeightCm), FBestThreeSession::Paid(Taken), Taken.RepeatFactor, Session.GetTotal());
			}
		}
		SeenRecordCount = Count;
	}

	const float Clock = Board.IsValid() ? Board->GetSimTimeSeconds() : LastClockSeconds + FMath::Max(FallbackDeltaSeconds, 0.0f);
	const float Delta = FMath::Max(Clock - LastClockSeconds, 0.0f);
	LastClockSeconds = Clock;
	const FJumpRecord* JumpInAir = (Source && Source->IsJumpInProgress()) ? &Source->GetLiveJump() : nullptr;
	Session.Tick(Delta, JumpInAir);

	if (!bWasFinished && Session.IsFinished())
	{
		OnFinished();
	}
}

void UTrickSessionSubsystem::OnFinished()
{
	const float Total = Session.GetTotal();
	const int32 Key = GetDurationKey();
	ReadLocalBest();
	bNewBest = Total > 0.0f && (!bHadPreviousBest || Total > PreviousBest);

	const UWorld* World = GetWorld();
	if (UKiteSurfGameInstance* GameInstance = World ? World->GetGameInstance<UKiteSurfGameInstance>() : nullptr)
	{
		if (GameInstance->RecordSessionTotal(Key, Total))
		{
			// The settings save path: it also carries the trick book.
			GameInstance->SaveSettingsToDisk();
		}
	}
	UE_LOG(LogKiteSurf, Log, TEXT("Best-three session: %d s over, %d jumps, total %.1f%s (local best before %.1f)"), Key,
		Session.GetJumps().Num(), Total, bNewBest ? TEXT(", NEW BEST") : TEXT(""), PreviousBest);
}
