#include "School/LessonSubsystem.h"
#include "School/LessonCatalog.h"
#include "School/LessonDirector.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "KiteRiderPawn.h"
#include "KiteSurfHUD.h"
#include "UI/KiteSurfMainMenuGameMode.h"
#include "UI/KiteSurfMainMenuWidget.h"
#include "UI/KiteSurfPauseMenuWidget.h"
#include "UI/KiteSurfSchoolWidget.h"
#include "UI/KiteSurfGameInstance.h"
#include "UI/KiteSurfSaveGame.h"

DEFINE_LOG_CATEGORY(LogKiteSchool);

bool ULessonSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Outer && Outer->IsA<UKiteSurfGameInstance>();
}

bool ULessonSubsystem::RecordLessonResult(FName LessonId, bool bPassed, int32 Stars, float Value, bool bNoAssists)
{
	if (!LessonCatalog::Find(LessonId))
	{
		UE_LOG(LogKiteSchool, Warning, TEXT("RecordLessonResult: no lesson %s in the catalogue; ignored"), *LessonId.ToString());
		return false;
	}
	const bool bRaised = Progress.RecordAttempt(LessonId, bPassed, Stars, Value, bNoAssists, FDateTime::UtcNow());
	// Before the save, so what the listeners change (the tutorial's completed flag) is written with it.
	OnLessonResultRecorded.Broadcast(LessonId, bPassed);
	SaveProgress();
	return bRaised;
}

bool ULessonSubsystem::RequestLessonMenu()
{
	if (!OnLessonMenuRequested.IsBound())
	{
		UE_LOG(LogKiteSchool, Display, TEXT("Lesson menu requested: no lesson menu bound (S5); falling back"));
		return false;
	}
	OnLessonMenuRequested.Broadcast();
	return true;
}

bool ULessonSubsystem::IsUnlocked(FName LessonId) const
{
	const FLessonDef* Lesson = LessonCatalog::Find(LessonId);
	return Lesson && LessonUnlock::IsUnlocked(*Lesson, Progress);
}

FName ULessonSubsystem::GetRecommendedNext() const
{
	return LessonUnlock::RecommendedNext(Progress);
}

float ULessonSubsystem::GetChapterCompletion(FName Chapter) const
{
	return Progress.ChapterCompletion(Chapter);
}

int32 ULessonSubsystem::GetTotalStars() const
{
	return Progress.TotalStars();
}

TArray<FLessonListItem> ULessonSubsystem::GetLessonList() const
{
	TArray<FLessonListItem> Items;
	for (const FLessonDef& Lesson : LessonCatalog::GetAll())
	{
		FLessonListItem& Item = Items.AddDefaulted_GetRef();
		Item.LessonId = Lesson.Id;
		Item.Title = Lesson.Title;
		Item.Chapter = Lesson.Chapter;
		Item.Stars = Progress.GetStars(Lesson.Id);
		Item.bAvailable = LessonCatalog::IsAvailable(Lesson);
		Item.bLocked = !LessonUnlock::IsUnlocked(Lesson, Progress);
		const FLessonRecord* Record = Progress.Find(Lesson.Id);
		Item.bNew = !Item.bLocked && (!Record || Record->Attempts == 0);
	}
	return Items;
}

void ULessonSubsystem::ResetProgress()
{
	Progress.Reset();
	SaveProgress();
}

bool ULessonSubsystem::StartLesson(FName LessonId)
{
	return StartLessonChecked(LessonId, true, FLessonRunOptions());
}

bool ULessonSubsystem::StartLessonWithOptions(FName LessonId, const FLessonRunOptions& Options)
{
	return StartLessonChecked(LessonId, true, Options);
}

bool ULessonSubsystem::StartLessonIgnoringPrerequisites(FName LessonId)
{
	return StartLessonChecked(LessonId, false, FLessonRunOptions());
}

bool ULessonSubsystem::StartLessonChecked(FName LessonId, bool bCheckPrerequisites, const FLessonRunOptions& Options)
{
	const FLessonDef* Lesson = LessonCatalog::Find(LessonId);
	if (!Lesson)
	{
		UE_LOG(LogKiteSchool, Log, TEXT("StartLesson: no lesson %s in the catalogue"), *LessonId.ToString());
		return false;
	}
	if (!LessonCatalog::IsAvailable(*Lesson))
	{
		UE_LOG(LogKiteSchool, Log, TEXT("StartLesson: %s needs a feature the game does not have yet"), *LessonId.ToString());
		return false;
	}
	if (bCheckPrerequisites && !LessonUnlock::IsUnlocked(*Lesson, Progress))
	{
		UE_LOG(LogKiteSchool, Log, TEXT("StartLesson: %s is locked"), *LessonId.ToString());
		return false;
	}
	PendingLessonId = LessonId;
	PendingRunOptions = Options;

	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (!World || !World->IsGameWorld())
	{
		UE_LOG(LogKiteSchool, Log, TEXT("StartLesson: %s is pending (no game world yet)"), *LessonId.ToString());
		return true;
	}
	// Already riding: run it here and now.
	const APlayerController* PC = World->GetFirstPlayerController();
	if (APawn* Rider = PC ? Cast<AKiteRiderPawn>(PC->GetPawn()) : nullptr)
	{
		UE_LOG(LogKiteSchool, Log, TEXT("StartLesson: %s starts in the ride in %s"), *LessonId.ToString(), *World->GetMapName());
		StartPendingLesson(Rider);
		return true;
	}
	if (!bTravelEnabled)
	{
		UE_LOG(LogKiteSchool, Log, TEXT("StartLesson: %s is pending (travel off)"), *LessonId.ToString());
		return true;
	}
	UE_LOG(LogKiteSchool, Log, TEXT("StartLesson: %s is pending; opening %s"), *LessonId.ToString(), *Lesson->Setup.Map.ToString());
	UGameplayStatics::OpenLevel(World, Lesson->Setup.Map);
	return true;
}

ALessonDirector* ULessonSubsystem::StartPendingLesson(APawn* Rider)
{
	const FLessonDef* Lesson = LessonCatalog::Find(PendingLessonId);
	if (!Lesson || !Rider)
	{
		return nullptr;
	}
	const FLessonRunOptions Options = PendingRunOptions;
	ClearPendingLesson();
	return ALessonDirector::StartInWorld(Rider->GetWorld(), *Lesson, Rider, Options);
}

void ULessonSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// The lesson menu answers the result card's "Lesson menu" (RequestLessonMenu, S4).
	OnLessonMenuRequested.AddUObject(this, &ULessonSubsystem::OpenLessonMenuForRequest);
}

void ULessonSubsystem::OpenLessonMenuForRequest()
{
	const UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	const bool bOpened = OpenLessonMenuInWorld(World) != nullptr;
	UE_LOG(LogKiteSchool, Log, TEXT("Lesson menu requested: %s"), bOpened ? TEXT("opened") : TEXT("no menu or ride to open it in"));
}

UKiteSurfSchoolWidget* ULessonSubsystem::OpenLessonMenuInWorld(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	// The main menu level: the lesson menu replaces the main menu, as the gear screen does.
	if (const AKiteSurfMainMenuGameMode* MenuMode = World->GetAuthGameMode<AKiteSurfMainMenuGameMode>())
	{
		if (UKiteSurfMainMenuWidget* MainMenu = Cast<UKiteSurfMainMenuWidget>(MenuMode->ActiveMainMenuWidget))
		{
			MainMenu->OnSchoolClicked();
			return MainMenu->ActiveSchoolWidget;
		}
		return nullptr;
	}
	// A ride: pause it with the pause menu, then open the lesson menu from there.
	APlayerController* PC = World->GetFirstPlayerController();
	AKiteSurfHUD* HUD = PC ? Cast<AKiteSurfHUD>(PC->GetHUD()) : nullptr;
	if (!HUD)
	{
		// A world whose actors were never initialised for play keeps no controller list.
		TActorIterator<AKiteSurfHUD> It(World);
		HUD = It ? *It : nullptr;
	}
	if (!HUD)
	{
		UE_LOG(LogKiteSchool, Log, TEXT("OpenLessonMenuInWorld: no main menu and no ride HUD in %s"), *World->GetName());
		return nullptr;
	}
	// The HUD keeps its pointer after the pause menu resumes the ride by itself, so a menu that is
	// not up (the ride running, or the widget gone from the viewport) is opened afresh.
	const UKiteSurfPauseMenuWidget* Open = HUD->GetActivePauseMenuWidget();
	if (!Open || !UGameplayStatics::IsGamePaused(World) || (World->GetGameViewport() && !Open->IsInViewport()))
	{
		HUD->ShowPauseMenu();
	}
	UKiteSurfPauseMenuWidget* PauseMenu = HUD->GetActivePauseMenuWidget();
	if (!PauseMenu)
	{
		return nullptr;
	}
	PauseMenu->OnSchoolClicked();
	return PauseMenu->ActiveSchoolWidget;
}

namespace LessonSubsystemPrivate
{
	/** The game instance of the world being played (ride or menu). */
	UKiteSurfGameInstance* FindPlayedGameInstance(UWorld* Preferred)
	{
		if (Preferred && Preferred->IsGameWorld())
		{
			if (UKiteSurfGameInstance* GI = Cast<UKiteSurfGameInstance>(Preferred->GetGameInstance()))
			{
				return GI;
			}
		}
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (World && World->IsGameWorld())
				{
					if (UKiteSurfGameInstance* GI = Cast<UKiteSurfGameInstance>(World->GetGameInstance()))
					{
						return GI;
					}
				}
			}
		}
		return nullptr;
	}

	FAutoConsoleCommandWithWorldAndArgs LessonCommand(
		TEXT("kitesurf.Lesson"),
		TEXT("Starts a kite school lesson (ULessonSubsystem::StartLesson): in the ride if there is one, else it opens the lesson's map. 'force' skips the prerequisites. Usage: kitesurf.Lesson <Id, e.g. B2> [force]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!Args.IsValidIndex(0))
			{
				UE_LOG(LogKiteSchool, Warning, TEXT("Usage: kitesurf.Lesson <Id> [force]"));
				return;
			}
			UKiteSurfGameInstance* GI = FindPlayedGameInstance(World);
			ULessonSubsystem* Lessons = GI ? GI->GetSubsystem<ULessonSubsystem>() : nullptr;
			if (!Lessons)
			{
				UE_LOG(LogKiteSchool, Warning, TEXT("kitesurf.Lesson: no game running"));
				return;
			}
			const FName Id(*Args[0].ToUpper());
			const bool bForce = Args.IsValidIndex(1) && Args[1].Equals(TEXT("force"), ESearchCase::IgnoreCase);
			const bool bStarted = bForce ? Lessons->StartLessonIgnoringPrerequisites(Id) : Lessons->StartLesson(Id);
			UE_LOG(LogKiteSchool, Display, TEXT("kitesurf.Lesson %s%s: %s"), *Id.ToString(), bForce ? TEXT(" force") : TEXT(""),
				bStarted ? TEXT("started") : TEXT("not started (unknown or locked; 'force' skips the prerequisites)"));
		}));

	FAutoConsoleCommandWithWorldAndArgs LessonActionCommand(
		TEXT("kitesurf.LessonAction"),
		TEXT("Acts on the running lesson as the result card and the drop-back offer do: next, retry, dropback, menu or exit; 'timelimit <s>' sets the run's time limit (testing: a TIME UP card sooner). Usage: kitesurf.LessonAction <next|retry|dropback|menu|exit|timelimit <s>>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UKiteSurfGameInstance* GI = FindPlayedGameInstance(World);
			UWorld* GameWorld = GI ? GI->GetWorld() : nullptr;
			ALessonDirector* Director = nullptr;
			if (GameWorld)
			{
				for (TActorIterator<ALessonDirector> It(GameWorld); It; ++It)
				{
					if (It->IsRunning())
					{
						Director = *It;
						break;
					}
				}
			}
			const FString Action = Args.IsValidIndex(0) ? Args[0].ToLower() : FString();
			if (!Director)
			{
				UE_LOG(LogKiteSchool, Warning, TEXT("kitesurf.LessonAction %s: no lesson running"), *Action);
				return;
			}
			bool bDone = false;
			if (Action == TEXT("next"))          { bDone = Director->Next(); }
			else if (Action == TEXT("retry"))    { bDone = Director->Retry(); }
			else if (Action == TEXT("dropback")) { bDone = Director->AcceptDropBack(); }
			else if (Action == TEXT("exit"))     { Director->ExitToFreeRide(); bDone = true; }
			else if (Action == TEXT("timelimit") && Args.IsValidIndex(1))
			{
				Director->LessonTimeLimitSeconds = FMath::Max(FCString::Atof(*Args[1]), 0.0f);
				bDone = true;
			}
			else if (Action == TEXT("menu"))
			{
				ULessonSubsystem* Lessons = GI->GetSubsystem<ULessonSubsystem>();
				bDone = Lessons && Lessons->RequestLessonMenu();
			}
			else
			{
				UE_LOG(LogKiteSchool, Warning, TEXT("Usage: kitesurf.LessonAction <next|retry|dropback|menu|exit|timelimit <s>>"));
				return;
			}
			UE_LOG(LogKiteSchool, Display, TEXT("kitesurf.LessonAction %s: %s"), *Action, bDone ? TEXT("done") : TEXT("not possible now"));
		}));

	FAutoConsoleCommandWithWorldAndArgs LessonResultCommand(
		TEXT("kitesurf.LessonResult"),
		TEXT("Records a finished lesson attempt as if it had been played (ULessonSubsystem::RecordLessonResult), for checking the lesson menu: 1 to 3 stars is a pass, 0 a failed attempt. Writes the save. Usage: kitesurf.LessonResult <Id> <stars 0-3>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UKiteSurfGameInstance* GI = FindPlayedGameInstance(World);
			ULessonSubsystem* Lessons = GI ? GI->GetSubsystem<ULessonSubsystem>() : nullptr;
			if (!Lessons || !Args.IsValidIndex(1))
			{
				UE_LOG(LogKiteSchool, Warning, TEXT("Usage: kitesurf.LessonResult <Id> <stars 0-3> (in a running game)"));
				return;
			}
			const FName Id(*Args[0].ToUpper());
			const int32 Stars = FMath::Clamp(FCString::Atoi(*Args[1]), 0, FLessonProgressBook::MaxStars);
			Lessons->RecordLessonResult(Id, Stars > 0, Stars, NAN, Stars == FLessonProgressBook::MaxStars);
			UE_LOG(LogKiteSchool, Display, TEXT("kitesurf.LessonResult %s: %s, best stars now %d"), *Id.ToString(),
				Stars > 0 ? TEXT("pass") : TEXT("failed attempt"), Lessons->GetProgress().GetStars(Id));
		}));

	FAutoConsoleCommandWithWorldAndArgs OpenSchoolCommand(
		TEXT("kitesurf.OpenSchool"),
		TEXT("Opens the lesson menu (ULessonSubsystem::OpenLessonMenuInWorld): over the main menu, or over the paused ride."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			UKiteSurfGameInstance* GI = FindPlayedGameInstance(World);
			if (!ULessonSubsystem::OpenLessonMenuInWorld(GI ? GI->GetWorld() : nullptr))
			{
				UE_LOG(LogKiteSchool, Warning, TEXT("kitesurf.OpenSchool: no menu or ride to open the lesson menu in"));
			}
		}));
}

void ULessonSubsystem::LoadFromSaveGame(const UKiteSurfSaveGame& SaveGame)
{
	Progress.SetEntries(SaveGame.LessonProgress.GetEntries());
}

void ULessonSubsystem::WriteToSaveGame(UKiteSurfSaveGame& SaveGame) const
{
	SaveGame.LessonProgress = Progress;
}

void ULessonSubsystem::SaveProgress()
{
	if (!bWriteToDisk)
	{
		return;
	}
	if (UKiteSurfGameInstance* GameInstance = Cast<UKiteSurfGameInstance>(GetGameInstance()))
	{
		GameInstance->SaveSettingsToDisk();
	}
}
