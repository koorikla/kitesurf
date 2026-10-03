#include "School/SchoolOnboarding.h"
#include "School/LessonCatalog.h"
#include "School/LessonDirector.h"
#include "School/LessonHUD.h"
#include "School/LessonSubsystem.h"
#include "UI/KiteSurfGameInstance.h"

const FName USchoolOnboardingSubsystem::FirstLessonId(TEXT("A1"));
const FName USchoolOnboardingSubsystem::LastLessonId(TEXT("A3"));
const FName USchoolOnboardingSubsystem::FirstJumpLessonId(TEXT("B2"));

namespace SchoolOnboardingPrivate
{
	const FName TutorialLessons[] = { TEXT("A1"), TEXT("A2"), TEXT("A3") };

	FString LessonName(FName LessonId)
	{
		const FLessonDef* Lesson = LessonCatalog::Find(LessonId);
		return Lesson ? FString::Printf(TEXT("%s %s"), *LessonId.ToString(), *Lesson->Title.ToString()) : LessonId.ToString();
	}
}

bool USchoolOnboardingSubsystem::IsTutorialLessonId(FName LessonId)
{
	return TutorialLessonNumber(LessonId) > 0;
}

int32 USchoolOnboardingSubsystem::TutorialLessonNumber(FName LessonId)
{
	for (int32 I = 0; I < UE_ARRAY_COUNT(SchoolOnboardingPrivate::TutorialLessons); ++I)
	{
		if (SchoolOnboardingPrivate::TutorialLessons[I] == LessonId)
		{
			return I + 1;
		}
	}
	return 0;
}

bool USchoolOnboardingSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Outer && Outer->IsA<UKiteSurfGameInstance>();
}

void USchoolOnboardingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	BindLessons(Collection.InitializeDependency<ULessonSubsystem>());
}

void USchoolOnboardingSubsystem::Deinitialize()
{
	BindLessons(nullptr);
	Super::Deinitialize();
}

void USchoolOnboardingSubsystem::SetLessonSubsystem(ULessonSubsystem* InLessons)
{
	LessonsOverride = InLessons;
	BindLessons(InLessons);
}

void USchoolOnboardingSubsystem::BindLessons(ULessonSubsystem* InLessons)
{
	if (ULessonSubsystem* Old = BoundLessons.Get())
	{
		Old->OnLessonResultRecorded.Remove(ResultHandle);
	}
	ResultHandle.Reset();
	BoundLessons = InLessons;
	if (InLessons)
	{
		ResultHandle = InLessons->OnLessonResultRecorded.AddUObject(this, &USchoolOnboardingSubsystem::HandleLessonResult);
	}
}

UKiteSurfGameInstance* USchoolOnboardingSubsystem::GetKiteGameInstance() const
{
	return Cast<UKiteSurfGameInstance>(GetOuter());
}

ULessonSubsystem* USchoolOnboardingSubsystem::GetLessons() const
{
	if (LessonsOverride)
	{
		return LessonsOverride;
	}
	const UKiteSurfGameInstance* GI = GetKiteGameInstance();
	return GI ? GI->GetSubsystem<ULessonSubsystem>() : nullptr;
}

bool USchoolOnboardingSubsystem::NeedsFirstRunTutorial() const
{
	const UKiteSurfGameInstance* GI = GetKiteGameInstance();
	return GI && !GI->bSkipOnboarding && !GI->bOnboardingCompleted;
}

FName USchoolOnboardingSubsystem::GetTutorialStartLessonId() const
{
	const ULessonSubsystem* Lessons = GetLessons();
	if (!Lessons)
	{
		return FirstLessonId;
	}
	for (const FName& Id : SchoolOnboardingPrivate::TutorialLessons)
	{
		if (Lessons->GetProgress().GetStars(Id) == 0)
		{
			// The first lesson without a pass; the ones before it are passed, so it is unlocked.
			return Lessons->IsUnlocked(Id) ? Id : FirstLessonId;
		}
	}
	return NAME_None;
}

bool USchoolOnboardingSubsystem::StartFirstRunTutorial()
{
	if (!NeedsFirstRunTutorial())
	{
		return false;
	}
	UKiteSurfGameInstance* GI = GetKiteGameInstance();
	ULessonSubsystem* Lessons = GetLessons();
	if (!Lessons)
	{
		return false;
	}
	const FName Start = GetTutorialStartLessonId();
	if (Start.IsNone())
	{
		// A1 to A3 passed already (from the School menu): nothing left to teach.
		UE_LOG(LogKiteSchool, Log, TEXT("First-run tutorial: A1 to A3 are passed; marking it completed"));
		GI->SetOnboardingCompleted(true);
		if (bWriteToDisk)
		{
			GI->SaveSettingsToDisk();
		}
		return false;
	}
	bTutorialRunning = true;
	bCompletedThisSession = false;
	if (!Lessons->StartLesson(Start))
	{
		bTutorialRunning = false;
		UE_LOG(LogKiteSchool, Warning, TEXT("First-run tutorial: %s did not start"), *Start.ToString());
		return false;
	}
	UE_LOG(LogKiteSchool, Display, TEXT("First-run tutorial: starting %s"), *Start.ToString());
	return true;
}

void USchoolOnboardingSubsystem::SkipTutorial(ALessonDirector* Director)
{
	UKiteSurfGameInstance* GI = GetKiteGameInstance();
	bTutorialRunning = false;
	if (GI)
	{
		GI->SetSkipOnboarding(true);
	}
	UE_LOG(LogKiteSchool, Display, TEXT("First-run tutorial: skipped; free ride"));
	if (Director && Director->IsRunning())
	{
		Director->ExitToFreeRide();
	}
	if (GI && bWriteToDisk)
	{
		GI->SaveSettingsToDisk();
	}
}

bool USchoolOnboardingSubsystem::IsTutorialLesson(const ALessonDirector* Director) const
{
	return bTutorialRunning && Director && Director->IsRunning() && IsTutorialLessonId(Director->GetLessonId());
}

void USchoolOnboardingSubsystem::HandleLessonResult(FName LessonId, bool bPassed)
{
	if (!bPassed || LessonId != LastLessonId)
	{
		return;
	}
	UKiteSurfGameInstance* GI = GetKiteGameInstance();
	if (GI && !GI->bOnboardingCompleted)
	{
		// ULessonSubsystem writes the save right after this, with the flag in it.
		GI->SetOnboardingCompleted(true);
		bCompletedThisSession = true;
		UE_LOG(LogKiteSchool, Display, TEXT("First-run tutorial: completed (A3 passed)"));
	}
	bTutorialRunning = false;
}

TArray<FString> USchoolOnboardingSubsystem::GetHintLines(const ALessonDirector* Director) const
{
	using namespace SchoolOnboardingPrivate;
	TArray<FString> Lines;
	if (!Director || !Director->IsRunning())
	{
		return Lines;
	}
	const FName LessonId = Director->GetLessonId();
	const bool bCard = Director->GetPhase() == ELessonPhase::Result
		&& (Director->GetOutcome() == ELessonOutcome::Passed || Director->GetOutcome() == ELessonOutcome::Failed);
	const FString Pause = LessonHUD::GlyphText(TEXT("IA_Pause"));

	if (bCompletedThisSession && LessonId == LastLessonId && bCard && Director->GetOutcome() == ELessonOutcome::Passed)
	{
		Lines.Add(TEXT("Tutorial complete: you have the basics."));
		const FName NextId = Director->GetNextLessonId();
		if (!NextId.IsNone())
		{
			Lines.Add(FString::Printf(TEXT("Continue the school: %s next lesson, %s"), *LessonHUD::GlyphText(TEXT("IA_Jump")), *LessonName(NextId)));
		}
		const ULessonSubsystem* Lessons = Director->GetLessonSubsystem();
		if (Lessons && Lessons->IsUnlocked(FirstJumpLessonId))
		{
			Lines.Add(FString::Printf(TEXT("Your first jump: %s is open in the lesson menu %s"), *LessonName(FirstJumpLessonId), *Pause));
		}
		else
		{
			Lines.Add(FString::Printf(TEXT("Jumps start at %s. Every lesson is in the lesson menu %s"), *LessonName(TEXT("B1")), *Pause));
		}
		Lines.Add(TEXT("Or free ride: FREE RIDE in the pause menu"));
		return Lines;
	}

	if (bTutorialRunning && !bCard && IsTutorialLessonId(LessonId))
	{
		const int32 Number = TutorialLessonNumber(LessonId);
		Lines.Add(Number == 1
			? FString(TEXT("Welcome to kite school: lesson 1 of the basics."))
			: FString::Printf(TEXT("Kite school: lesson %d of the basics."), Number));
		Lines.Add(FString::Printf(TEXT("Skip tutorial: %s, then SKIP TUTORIAL"), *Pause));
	}
	return Lines;
}
