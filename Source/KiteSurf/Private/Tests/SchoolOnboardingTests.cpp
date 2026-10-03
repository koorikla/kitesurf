#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "KiteRiderPawn.h"
#include "KiteSurfHUD.h"
#include "KiteSurfUnits.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/SecureHash.h"
#include "School/LessonCatalog.h"
#include "School/LessonDirector.h"
#include "School/LessonSubsystem.h"
#include "School/SchoolOnboarding.h"
#include "UI/KiteSurfGameInstance.h"
#include "UI/KiteSurfGearWidget.h"
#include "UI/KiteSurfMainMenuWidget.h"
#include "UI/KiteSurfMenuNavigator.h"
#include "UI/KiteSurfPauseMenuWidget.h"
#include "UI/KiteSurfSaveGame.h"
#include "WindComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests on the first-run tutorial (docs/tutorials.md S7): lessons A1 to A3 replace the old four-step
// HUD onboarding. PLAY on a first run starts A1 (the pending lesson, travel off), SKIP TUTORIAL goes to
// free ride and sets bSkipOnboarding, completed or skipped onboarding starts nothing, passing A3 sets
// bOnboardingCompleted, and the old prompt is gone from the HUD. Every subsystem here has disk writes
// and travel off, and the tests that use a real game instance check that the player's Settings save
// is unchanged.

// Named, not anonymous, so a unity build cannot merge these with another file's helpers.
namespace SchoolOnboardingTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	const float TutorialFrameSeconds = 1.0f / 60.0f;

	/** A game instance that was never initialised (no disk read), with a lesson subsystem and the tutorial on it. */
	struct FBareTutorial
	{
		UKiteSurfGameInstance* GI = nullptr;
		ULessonSubsystem* Lessons = nullptr;
		USchoolOnboardingSubsystem* Onboarding = nullptr;

		FBareTutorial()
		{
			GI = NewObject<UKiteSurfGameInstance>();
			Lessons = NewObject<ULessonSubsystem>(GI);
			Lessons->SetWriteToDisk(false);
			Lessons->SetTravelEnabled(false);
			Onboarding = NewObject<USchoolOnboardingSubsystem>(GI);
			Onboarding->SetWriteToDisk(false);
			Onboarding->SetLessonSubsystem(Lessons);
		}
	};

	/** The player's Settings save, before and after: it must come out of a test unchanged. */
	struct FTutorialSettingsGuard
	{
		FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), UKiteSurfSaveGame::DefaultSaveSlot + TEXT(".sav"));
		bool bExisted = false;
		FDateTime Stamp;
		FString Hash;

		FTutorialSettingsGuard()
		{
			bExisted = IFileManager::Get().FileExists(*Path);
			Stamp = IFileManager::Get().GetTimeStamp(*Path);
			Hash = bExisted ? LexToString(FMD5Hash::HashFile(*Path)) : FString();
		}

		void Check(FAutomationTestBase& Test) const
		{
			const bool bExists = IFileManager::Get().FileExists(*Path);
			Test.TestEqual(TEXT("The Settings save is neither made nor removed"), bExists, bExisted);
			if (bExisted && bExists)
			{
				Test.TestEqual(TEXT("The Settings save's contents are unchanged (MD5)"), LexToString(FMD5Hash::HashFile(*Path)), Hash);
				Test.TestTrue(TEXT("The Settings save's time stamp is unchanged"), IFileManager::Get().GetTimeStamp(*Path) == Stamp);
			}
		}
	};

	/**
	 * A game instance initialised as a standalone game (its own world, Init made the subsystems and read
	 * the Settings slot), then set to a first run in memory: both flags off, no lesson progress, no disk
	 * writes, no travel. Shut down with the fixture.
	 */
	struct FStandaloneTutorial
	{
		UKiteSurfGameInstance* GI = nullptr;
		UWorld* World = nullptr;
		ULessonSubsystem* Lessons = nullptr;
		USchoolOnboardingSubsystem* Onboarding = nullptr;
		float VolumeBefore = 1.0f;

		FStandaloneTutorial()
		{
			VolumeBefore = FApp::GetVolumeMultiplier(); // Init applies the Settings slot's volume
			GI = NewObject<UKiteSurfGameInstance>(GEngine);
			GI->InitializeStandalone();
			World = GI->GetWorld();
			Lessons = GI->GetSubsystem<ULessonSubsystem>();
			Onboarding = GI->GetSubsystem<USchoolOnboardingSubsystem>();
			if (Lessons && Onboarding)
			{
				Lessons->SetWriteToDisk(false);
				Lessons->SetTravelEnabled(false);
				Onboarding->SetWriteToDisk(false);
				Lessons->ResetProgress();
				GI->SetSkipOnboarding(false);
				GI->SetOnboardingCompleted(false);
			}
		}

		~FStandaloneTutorial()
		{
			if (Lessons)
			{
				Lessons->ClearPendingLesson();
			}
			if (GI)
			{
				GI->Shutdown();
			}
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
			FApp::SetVolumeMultiplier(VolumeBefore);
		}

		bool IsValid() const { return GI && World && Lessons && Onboarding; }

		/** A kite rider in steady 20 kn, possessed by a controller with a ride HUD. */
		AKiteRiderPawn* SpawnRide(AKiteSurfHUD*& OutHUD) const
		{
			OutHUD = nullptr;
			APlayerController* PC = World->SpawnActor<APlayerController>();
			AKiteSurfHUD* HUD = World->SpawnActor<AKiteSurfHUD>();
			AKiteRiderPawn* Rider = World->SpawnActor<AKiteRiderPawn>();
			if (!PC || !HUD || !Rider)
			{
				return nullptr;
			}
			Rider->bInterpolateRendering = false;
			if (UWindComponent* Wind = Rider->GetWind())
			{
				Wind->BaseWind = FVector(KiteUnits::KnotsToCmS(20.0f), 0.0f, 0.0f);
				Wind->GustStrength = 0.0f;
				Wind->DirectionDriftDeg = 0.0f;
			}
			PC->SetPlayerState(World->SpawnActor<APlayerState>());
			HUD->PlayerOwner = PC;
			PC->MyHUD = HUD;
			PC->Possess(Rider);
			OutHUD = HUD;
			return Rider;
		}
	};

	/** A catalogue lesson cut to one step that passes on its first judged sample (any speed), so its result card comes at once. */
	FLessonDef TutorialQuickPass(const TCHAR* Id)
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

	/** Steps the rider and the director until the director has a result card, for up to Seconds. */
	bool TutorialRideToCard(AKiteRiderPawn* Rider, ALessonDirector* Director, float Seconds)
	{
		for (float T = 0.0f; T < Seconds; T += TutorialFrameSeconds)
		{
			Rider->Tick(TutorialFrameSeconds);
			Director->UpdateLesson(TutorialFrameSeconds);
			if (Director->GetPhase() == ELessonPhase::Result
				&& (Director->GetOutcome() == ELessonOutcome::Passed || Director->GetOutcome() == ELessonOutcome::Failed))
			{
				return true;
			}
		}
		return false;
	}

	bool TutorialLineContains(const TArray<FString>& Lines, const TCHAR* Text)
	{
		return Lines.ContainsByPredicate([Text](const FString& L) { return L.Contains(Text); });
	}
}

using namespace SchoolOnboardingTest;

// A first run (neither completed nor skipped) starts A1: the subsystem keeps A1 as the pending lesson
// (travel is off; in the game L_FlatWater opens and the game mode starts it after the ride set-up), and
// the main menu's PLAY takes that path instead of the gear screen. With onboarding skipped, PLAY opens
// the gear screen as before.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolOnboardingFirstRunStartsA1, "KiteSurf.School.OnboardingFirstRunStartsA1", SchoolOnboardingTest::Flags)

bool FKiteSurfSchoolOnboardingFirstRunStartsA1::RunTest(const FString& Parameters)
{
	{
		FBareTutorial T;
		TestTrue(TEXT("A fresh game instance needs the tutorial"), T.Onboarding->NeedsFirstRunTutorial());
		TestEqual(TEXT("It starts with A1"), T.Onboarding->GetTutorialStartLessonId(), FName(TEXT("A1")));
		TestFalse(TEXT("Not running before PLAY"), T.Onboarding->IsTutorialRunning());
		TestTrue(TEXT("StartFirstRunTutorial starts it"), T.Onboarding->StartFirstRunTutorial());
		TestEqual(TEXT("A1 is the pending lesson"), T.Lessons->GetPendingLessonId(), FName(TEXT("A1")));
		TestTrue(TEXT("The tutorial is running"), T.Onboarding->IsTutorialRunning());
		TestFalse(TEXT("Starting it sets neither flag"), T.GI->bSkipOnboarding || T.GI->bOnboardingCompleted);

		// A player who left part-way (A1 passed) resumes at A2.
		T.Lessons->ClearPendingLesson();
		T.Lessons->RecordLessonResult(TEXT("A1"), true, 1, NAN, false);
		TestEqual(TEXT("A1 passed: the tutorial resumes at A2"), T.Onboarding->GetTutorialStartLessonId(), FName(TEXT("A2")));
		TestTrue(TEXT("and starts it"), T.Onboarding->StartFirstRunTutorial() && T.Lessons->GetPendingLessonId() == FName(TEXT("A2")));
	}

	// The main menu's PLAY on a real game instance.
	const FTutorialSettingsGuard Settings;
	{
		FStandaloneTutorial T;
		if (!TestTrue(TEXT("A standalone game instance with the lesson subsystem and the tutorial"), T.IsValid()))
		{
			return false;
		}
		UKiteSurfMainMenuWidget* MainMenu = CreateWidget<UKiteSurfMainMenuWidget>(T.World, UKiteSurfMainMenuWidget::StaticClass());
		if (!TestNotNull(TEXT("Main menu created"), MainMenu))
		{
			return false;
		}
		MainMenu->GetNavigator().Select(0);
		MainMenu->GetNavigator().HandleKey(EKeys::Enter);
		TestEqual(TEXT("PLAY on a first run: A1 is pending"), T.Lessons->GetPendingLessonId(), FName(TEXT("A1")));
		TestNull(TEXT("and there is no gear screen"), MainMenu->ActiveGearWidget.Get());
		TestTrue(TEXT("The tutorial is running"), T.Onboarding->IsTutorialRunning());

		// Skipped: PLAY is the gear screen and free ride again.
		T.Lessons->ClearPendingLesson();
		T.GI->SetSkipOnboarding(true);
		MainMenu->OnPlayClicked();
		TestTrue(TEXT("Skipped: nothing pending"), T.Lessons->GetPendingLessonId().IsNone());
		if (TestNotNull(TEXT("Skipped: PLAY opens the gear screen"), MainMenu->ActiveGearWidget.Get()))
		{
			MainMenu->ActiveGearWidget->Cancel();
		}
	}
	Settings.Check(*this);
	return true;
}

// SKIP TUTORIAL: during A1 the HUD shows the welcome and how to skip, the pause menu's FREE RIDE reads
// SKIP TUTORIAL, and choosing it ends the lesson in free ride, sets bSkipOnboarding and leaves nothing
// to start on the next PLAY.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolOnboardingSkipGoesToFreeRide, "KiteSurf.School.OnboardingSkipGoesToFreeRide", SchoolOnboardingTest::Flags)

bool FKiteSurfSchoolOnboardingSkipGoesToFreeRide::RunTest(const FString& Parameters)
{
	{
		// The subsystem alone, with no lesson running.
		FBareTutorial T;
		T.Onboarding->StartFirstRunTutorial();
		T.Lessons->ClearPendingLesson();
		T.Onboarding->SkipTutorial(nullptr);
		TestTrue(TEXT("Skip sets bSkipOnboarding"), T.GI->bSkipOnboarding);
		TestFalse(TEXT("and not bOnboardingCompleted"), T.GI->bOnboardingCompleted);
		TestFalse(TEXT("The tutorial no longer runs"), T.Onboarding->IsTutorialRunning());
		TestFalse(TEXT("Nothing to start on the next PLAY"), T.Onboarding->StartFirstRunTutorial());
		TestTrue(TEXT("and nothing pending"), T.Lessons->GetPendingLessonId().IsNone());
	}

	const FTutorialSettingsGuard Settings;
	{
		FStandaloneTutorial T;
		if (!TestTrue(TEXT("A standalone game instance with the lesson subsystem and the tutorial"), T.IsValid()))
		{
			return false;
		}
		AKiteSurfHUD* HUD = nullptr;
		AKiteRiderPawn* Rider = T.SpawnRide(HUD);
		if (!TestTrue(TEXT("Controller, HUD and rider spawned"), Rider && HUD))
		{
			return false;
		}
		TestTrue(TEXT("The first-run tutorial starts"), T.Onboarding->StartFirstRunTutorial());
		// In the game L_FlatWater opens and AKiteSurfGameMode::HandleStartingNewPlayer starts the pending
		// lesson once the rider is set up; here (travel off) the test does what the game mode does.
		if (T.Lessons->GetPendingLessonId() == FName(TEXT("A1")))
		{
			T.Lessons->StartPendingLesson(Rider);
		}
		ALessonDirector* Director = HUD->FindLessonDirector();
		if (!TestNotNull(TEXT("A1 runs in the ride"), Director))
		{
			return false;
		}
		TestEqual(TEXT("The lesson is A1"), Director->GetLessonId(), FName(TEXT("A1")));

		HUD->UpdateTutorialHint();
		const TArray<FString>& Hint = HUD->GetTutorialHintLines();
		TestTrue(TEXT("The HUD welcomes the player"), Hint.Num() == 2 && Hint[0] == TEXT("Welcome to kite school: lesson 1 of the basics."));
		TestTrue(TEXT("and says how to skip"), TutorialLineContains(Hint, TEXT("Skip tutorial: [Esc | Start], then SKIP TUTORIAL")));

		HUD->ShowPauseMenu();
		UKiteSurfPauseMenuWidget* Pause = HUD->GetActivePauseMenuWidget();
		if (!TestNotNull(TEXT("The pause menu opens"), Pause))
		{
			return false;
		}
		TestTrue(TEXT("It shows the lesson items"), Pause->ShowsLessonItems());
		TestEqual(TEXT("The same ten items as in any lesson"), Pause->GetNavigator().Num(), 10);
		TestTrue(TEXT("FREE RIDE reads SKIP TUTORIAL"), Pause->ShowsSkipTutorial());
		TestEqual(TEXT("Its label"), Pause->GetFreeRideLabel().ToString(), FString(TEXT("SKIP TUTORIAL")));
		Pause->GetNavigator().Select(6);
		Pause->GetNavigator().HandleKey(EKeys::Enter);

		TestTrue(TEXT("SKIP TUTORIAL ends the lesson: free ride"), !IsValid(Director) || !Director->IsRunning());
		TestNull(TEXT("No lesson runs"), HUD->FindLessonDirector());
		TestFalse(TEXT("The ride resumes"), UGameplayStatics::IsGamePaused(T.World));
		TestTrue(TEXT("bSkipOnboarding is set"), T.GI->bSkipOnboarding);
		TestFalse(TEXT("The tutorial no longer runs"), T.Onboarding->IsTutorialRunning());
		HUD->UpdateTutorialHint();
		TestEqual(TEXT("The tutorial line is gone"), HUD->GetTutorialHintLines().Num(), 0);
		TestFalse(TEXT("PLAY starts no lesson now"), T.Onboarding->StartFirstRunTutorial());

		// A lesson started later from the School menu has a plain FREE RIDE.
		Director = ALessonDirector::StartInWorld(T.World, *LessonCatalog::Find(TEXT("A1")), Rider);
		HUD->ShowPauseMenu();
		Pause = HUD->GetActivePauseMenuWidget();
		if (TestNotNull(TEXT("A School menu lesson runs and the pause menu opens"), Director ? Pause : nullptr))
		{
			TestEqual(TEXT("Outside the tutorial it is FREE RIDE"), Pause->GetFreeRideLabel().ToString(), FString(TEXT("FREE RIDE")));
		}
		HUD->UpdateTutorialHint();
		TestEqual(TEXT("and there is no tutorial line"), HUD->GetTutorialHintLines().Num(), 0);
		HUD->HidePauseMenu();
		if (Director)
		{
			Director->ExitToFreeRide();
		}
	}
	Settings.Check(*this);
	return true;
}

// Completed or skipped onboarding starts nothing, and a save whose player already passed A1 to A3 (from
// the School menu) is marked completed rather than sent back to A1.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolOnboardingCompletedDoesNotStartA1, "KiteSurf.School.OnboardingCompletedDoesNotStartA1", SchoolOnboardingTest::Flags)

bool FKiteSurfSchoolOnboardingCompletedDoesNotStartA1::RunTest(const FString& Parameters)
{
	{
		FBareTutorial T;
		T.GI->SetOnboardingCompleted(true);
		TestFalse(TEXT("Completed: no tutorial needed"), T.Onboarding->NeedsFirstRunTutorial());
		TestFalse(TEXT("Completed: StartFirstRunTutorial starts nothing"), T.Onboarding->StartFirstRunTutorial());
		TestTrue(TEXT("Completed: nothing pending"), T.Lessons->GetPendingLessonId().IsNone());
		TestFalse(TEXT("Completed: not running"), T.Onboarding->IsTutorialRunning());
	}
	{
		FBareTutorial T;
		T.GI->SetSkipOnboarding(true);
		TestFalse(TEXT("Skipped: no tutorial needed"), T.Onboarding->NeedsFirstRunTutorial());
		TestFalse(TEXT("Skipped: StartFirstRunTutorial starts nothing"), T.Onboarding->StartFirstRunTutorial());
		TestTrue(TEXT("Skipped: nothing pending"), T.Lessons->GetPendingLessonId().IsNone());
	}
	{
		FBareTutorial T;
		// Without the tutorial listening, so the flag stays off as in an old save.
		T.Onboarding->SetLessonSubsystem(nullptr);
		for (const TCHAR* Id : { TEXT("A1"), TEXT("A2"), TEXT("A3") })
		{
			T.Lessons->RecordLessonResult(Id, true, 1, NAN, false);
		}
		T.Onboarding->SetLessonSubsystem(T.Lessons);
		TestFalse(TEXT("The flag is still off"), T.GI->bOnboardingCompleted);
		TestTrue(TEXT("A1 to A3 passed: nothing left to start"), T.Onboarding->GetTutorialStartLessonId().IsNone());
		TestFalse(TEXT("StartFirstRunTutorial starts nothing"), T.Onboarding->StartFirstRunTutorial());
		TestTrue(TEXT("and nothing is pending"), T.Lessons->GetPendingLessonId().IsNone());
		TestTrue(TEXT("but marks the tutorial completed"), T.GI->bOnboardingCompleted);
	}
	return true;
}

// The tutorial runs A1, A2 and A3 with the result card's Next; passing A3 sets bOnboardingCompleted (and
// the save gets it), and A3's card offers to continue the school, the first jump and free ride. With B2
// unlocked the first jump points to it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolOnboardingPassingA3Completes, "KiteSurf.School.OnboardingPassingA3Completes", SchoolOnboardingTest::Flags)

bool FKiteSurfSchoolOnboardingPassingA3Completes::RunTest(const FString& Parameters)
{
	{
		// The result hook alone.
		FBareTutorial T;
		T.Lessons->RecordLessonResult(TEXT("A1"), true, 1, NAN, false);
		T.Lessons->RecordLessonResult(TEXT("A2"), true, 1, NAN, false);
		TestFalse(TEXT("A1 and A2 do not complete it"), T.GI->bOnboardingCompleted);
		T.Lessons->RecordLessonResult(TEXT("A3"), false, 0, NAN, false);
		TestFalse(TEXT("A failed A3 does not complete it"), T.GI->bOnboardingCompleted);
		T.Lessons->RecordLessonResult(TEXT("A3"), true, 1, NAN, false);
		TestTrue(TEXT("Passing A3 sets bOnboardingCompleted"), T.GI->bOnboardingCompleted);
		TestTrue(TEXT("this session"), T.Onboarding->WasCompletedThisSession());
		UKiteSurfSaveGame* Save = NewObject<UKiteSurfSaveGame>();
		T.GI->WriteToSaveGame(*Save);
		TestTrue(TEXT("The save carries it"), Save->bOnboardingCompleted);
		TestFalse(TEXT("PLAY starts no lesson now"), T.Onboarding->StartFirstRunTutorial());
	}

	// The whole chain on a rider: each lesson cut to a quick pass, Next between them as on the card.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (World && GEngine)
	{
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	}
	ON_SCOPE_EXIT
	{
		if (World)
		{
			if (GEngine)
			{
				GEngine->DestroyWorldContext(World);
			}
			World->DestroyWorld(false);
		}
	};
	AKiteRiderPawn* Rider = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
	ALessonDirector* Director = World ? World->SpawnActor<ALessonDirector>() : nullptr;
	if (!TestTrue(TEXT("Rider and director spawned"), Rider && Director))
	{
		return false;
	}
	Rider->bInterpolateRendering = false;
	if (UWindComponent* Wind = Rider->GetWind())
	{
		Wind->BaseWind = FVector(KiteUnits::KnotsToCmS(20.0f), 0.0f, 0.0f);
		Wind->GustStrength = 0.0f;
		Wind->DirectionDriftDeg = 0.0f;
	}
	FBareTutorial T;
	Director->SetLessonSubsystem(T.Lessons);
	Director->IntroSeconds = 0.0f;
	TestTrue(TEXT("The tutorial starts (A1 pending: no ride in the game instance's world)"), T.Onboarding->StartFirstRunTutorial());
	T.Lessons->ClearPendingLesson();

	const TCHAR* Chain[] = { TEXT("A1"), TEXT("A2"), TEXT("A3") };
	for (int32 I = 0; I < UE_ARRAY_COUNT(Chain); ++I)
	{
		const FName Id(Chain[I]);
		if (I > 0)
		{
			TestEqual(FString::Printf(TEXT("Next went on to %s"), Chain[I]), Director->GetLessonId(), Id);
		}
		TestTrue(FString::Printf(TEXT("%s begins (cut to a quick pass)"), Chain[I]), Director->BeginLesson(TutorialQuickPass(Chain[I]), Rider));
		// In its intro: the quick pass comes on the first judged sample.
		const TArray<FString> During = T.Onboarding->GetHintLines(Director);
		const FString Welcome = I == 0 ? FString(TEXT("Welcome to kite school: lesson 1 of the basics."))
			: FString::Printf(TEXT("Kite school: lesson %d of the basics."), I + 1);
		TestTrue(FString::Printf(TEXT("%s: the tutorial line names the lesson"), Chain[I]), During.Num() == 2 && During[0] == Welcome);
		TestTrue(FString::Printf(TEXT("%s: and how to skip"), Chain[I]), TutorialLineContains(During, TEXT("SKIP TUTORIAL")));
		if (!TestTrue(FString::Printf(TEXT("%s passes"), Chain[I]), TutorialRideToCard(Rider, Director, 3.0f) && Director->GetOutcome() == ELessonOutcome::Passed))
		{
			return false;
		}
		if (I < 2)
		{
			TestFalse(FString::Printf(TEXT("%s: not completed yet"), Chain[I]), T.GI->bOnboardingCompleted);
			TestEqual(FString::Printf(TEXT("%s: no tutorial line over its result card"), Chain[I]), T.Onboarding->GetHintLines(Director).Num(), 0);
			TestTrue(FString::Printf(TEXT("%s: the card's Next"), Chain[I]), Director->Next());
		}
	}
	TestTrue(TEXT("Passing A3 sets bOnboardingCompleted"), T.GI->bOnboardingCompleted);
	TestFalse(TEXT("The tutorial has finished"), T.Onboarding->IsTutorialRunning());

	TArray<FString> Done = T.Onboarding->GetHintLines(Director);
	TestTrue(TEXT("A3's card: tutorial complete"), Done.Num() == 4 && Done[0] == TEXT("Tutorial complete: you have the basics."));
	TestTrue(TEXT("Continue the school with the card's Next (A4)"), TutorialLineContains(Done, TEXT("Continue the school: [Space | A] next lesson, A4 Upwind")));
	TestTrue(TEXT("B2 locked: where jumps start, and the lesson menu"), TutorialLineContains(Done, TEXT("Jumps start at B1 Pop. Every lesson is in the lesson menu [Esc | Start]")));
	TestTrue(TEXT("Or free ride"), TutorialLineContains(Done, TEXT("FREE RIDE")));

	// A player with B1 already passed: the first jump is B2.
	T.Lessons->RecordLessonResult(TEXT("B1"), true, 1, NAN, false);
	Done = T.Onboarding->GetHintLines(Director);
	TestTrue(TEXT("B2 unlocked: the first jump points to it"), TutorialLineContains(Done, TEXT("Your first jump: B2 Small jump is open in the lesson menu [Esc | Start]")));

	TestTrue(TEXT("Next goes on to the school"), Director->Next());
	TestEqual(TEXT("A4 runs"), Director->GetLessonId(), FName(TEXT("A4")));
	TestEqual(TEXT("and the tutorial line is gone"), T.Onboarding->GetHintLines(Director).Num(), 0);
	Director->ExitToFreeRide();
	return true;
}

// The old four-step onboarding (steer, sheet, edge, jump) is gone from the HUD: none of its functions or
// properties exist, and a ride with no lesson on a first run shows no tutorial line.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolOnboardingOldPromptGone, "KiteSurf.School.OnboardingOldPromptGone", SchoolOnboardingTest::Flags)

bool FKiteSurfSchoolOnboardingOldPromptGone::RunTest(const FString& Parameters)
{
	const UClass* HUDClass = AKiteSurfHUD::StaticClass();
	for (const TCHAR* Name : { TEXT("StartOnboarding"), TEXT("SkipOnboarding"), TEXT("AdvanceOnboardingStep"), TEXT("GetCurrentPromptText"),
		TEXT("IsOnboardingActive"), TEXT("GetCurrentOnboardingStep"), TEXT("GetCurrentStepProgress") })
	{
		TestNull(FString::Printf(TEXT("No HUD function %s"), Name), HUDClass->FindFunctionByName(Name));
	}
	for (const TCHAR* Name : { TEXT("CurrentOnboardingStep"), TEXT("bOnboardingActive"), TEXT("CurrentStepProgress") })
	{
		TestNull(FString::Printf(TEXT("No HUD property %s"), Name), HUDClass->FindPropertyByName(Name));
	}

	const FTutorialSettingsGuard Settings;
	{
		FStandaloneTutorial T;
		if (!TestTrue(TEXT("A standalone game instance on a first run"), T.IsValid() && T.Onboarding->NeedsFirstRunTutorial()))
		{
			return false;
		}
		AKiteSurfHUD* HUD = nullptr;
		AKiteRiderPawn* Rider = T.SpawnRide(HUD);
		if (!TestTrue(TEXT("Controller, HUD and rider spawned"), Rider && HUD))
		{
			return false;
		}
		HUD->UpdateLessonLayer(TutorialFrameSeconds);
		HUD->UpdateTutorialHint();
		TestFalse(TEXT("Free ride: no lesson layer"), HUD->IsLessonLayerVisible());
		TestEqual(TEXT("Free ride on a first run: no tutorial line (the tutorial is the lessons)"), HUD->GetTutorialHintLines().Num(), 0);
		TestTrue(TEXT("and no lesson was started from the HUD"), T.Lessons->GetPendingLessonId().IsNone() && !HUD->FindLessonDirector());
	}
	Settings.Check(*this);
	return true;
}

#endif
