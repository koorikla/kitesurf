#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Blueprint/UserWidget.h"
#include "BoardMovementComponent.h"
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
#include "Misc/SecureHash.h"
#include "School/LessonCatalog.h"
#include "School/LessonDirector.h"
#include "School/LessonSubsystem.h"
#include "Tricks/JumpRecord.h"
#include "UI/KiteSurfGameInstance.h"
#include "UI/KiteSurfGearWidget.h"
#include "UI/KiteSurfMainMenuWidget.h"
#include "UI/KiteSurfMenuNavigator.h"
#include "UI/KiteSurfPauseMenuWidget.h"
#include "UI/KiteSurfSaveGame.h"
#include "UI/KiteSurfSchoolWidget.h"
#include "WindComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests on the kite school's lesson menu (S5, docs/tutorials.md 3.1): the tiles, the detail panel,
// keyboard and gamepad navigation, starting (locked lessons refused, Continue, rerun options), reset
// with its confirm step, the main menu's SCHOOL and the pause menu's lesson entries. Every lesson
// subsystem here is made in the test with disk writes and travel off, and the tests that could
// write check that the player's Settings save is unchanged.

// Named, not anonymous, so a unity build cannot merge these with another file's helpers.
namespace SchoolMenuTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	ULessonSubsystem* MakeMenuLessons(UKiteSurfGameInstance* GI = nullptr)
	{
		ULessonSubsystem* Lessons = NewObject<ULessonSubsystem>(GI ? GI : NewObject<UKiteSurfGameInstance>());
		Lessons->SetWriteToDisk(false);
		Lessons->SetTravelEnabled(false);
		return Lessons;
	}

	void PassLessons(ULessonSubsystem* Lessons, const TArray<const TCHAR*>& Ids, int32 Stars = 1)
	{
		for (const TCHAR* Id : Ids)
		{
			Lessons->RecordLessonResult(Id, true, Stars, NAN, false);
		}
	}

	/** The player's Settings save, before and after: it must come out of a test unchanged. */
	struct FMenuSettingsGuard
	{
		FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), UKiteSurfSaveGame::DefaultSaveSlot + TEXT(".sav"));
		bool bExisted = false;
		FDateTime Stamp;
		FString Hash;

		FMenuSettingsGuard()
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

	/** A game world with a context, so actors can be destroyed as in a game; destroyed with the fixture. */
	struct FMenuWorldFixture
	{
		UWorld* World = nullptr;

		FMenuWorldFixture()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (World && GEngine)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			}
		}

		~FMenuWorldFixture()
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

		UKiteSurfSchoolWidget* MakeSchool(ULessonSubsystem* Lessons) const
		{
			UKiteSurfSchoolWidget* School = World ? CreateWidget<UKiteSurfSchoolWidget>(World, UKiteSurfSchoolWidget::StaticClass()) : nullptr;
			if (School)
			{
				School->SetLessonSubsystem(Lessons);
			}
			return School;
		}

		/** A kite rider in steady 20 kn. */
		AKiteRiderPawn* SpawnRider() const
		{
			AKiteRiderPawn* Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
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
			return Pawn;
		}
	};
}

// The tiles show each lesson's progress state: stars, new, locked and coming soon; the detail panel
// and the overall progress say the same in words.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolMenuTilesShowProgress, "KiteSurf.School.MenuTilesShowProgress", SchoolMenuTest::Flags)

bool FKiteSurfSchoolMenuTilesShowProgress::RunTest(const FString& Parameters)
{
	using namespace SchoolMenuTest;
	FMenuWorldFixture Fx;
	ULessonSubsystem* Lessons = MakeMenuLessons();
	PassLessons(Lessons, { TEXT("A1") }, 3);
	PassLessons(Lessons, { TEXT("A2") }, 1);
	UKiteSurfSchoolWidget* School = Fx.MakeSchool(Lessons);
	if (!TestNotNull(TEXT("Lesson menu created"), School))
	{
		return false;
	}

	TestEqual(TEXT("A tile for every catalogue lesson"), School->GetTiles().Num(), LessonCatalog::GetAll().Num());
	const FLessonListItem* A1 = School->FindTile(TEXT("A1"));
	const FLessonListItem* A2 = School->FindTile(TEXT("A2"));
	const FLessonListItem* A3 = School->FindTile(TEXT("A3"));
	const FLessonListItem* A4 = School->FindTile(TEXT("A4"));
	const FLessonListItem* A6 = School->FindTile(TEXT("A6"));
	const FLessonListItem* B4 = School->FindTile(TEXT("B4"));
	if (!TestTrue(TEXT("Tiles for A1, A2, A3, A4, A6 and B4"), A1 && A2 && A3 && A4 && A6 && B4))
	{
		return false;
	}
	TestTrue(TEXT("A1: three stars, unlocked, no badge"), A1->Stars == 3 && !A1->bLocked && !A1->bNew && School->GetTileBadge(TEXT("A1")).IsEmpty());
	TestTrue(TEXT("A2: one star"), A2->Stars == 1 && !A2->bLocked);
	TestTrue(TEXT("A3: unlocked by A2, never played: NEW"), !A3->bLocked && A3->bNew && A3->Stars == 0);
	TestEqual(TEXT("A3's badge"), School->GetTileBadge(TEXT("A3")).ToString(), FString(TEXT("NEW")));
	TestTrue(TEXT("A4: locked behind A3"), A4->bLocked && A4->bAvailable && !A4->bNew);
	TestEqual(TEXT("A4's badge"), School->GetTileBadge(TEXT("A4")).ToString(), FString(TEXT("LOCKED")));
	TestTrue(TEXT("A6 and B4: their features are not built"), !A6->bAvailable && !B4->bAvailable && A6->bLocked);
	TestEqual(TEXT("A6's badge"), School->GetTileBadge(TEXT("A6")).ToString(), FString(TEXT("COMING SOON")));

	// The detail panel.
	TestEqual(TEXT("The menu opens on the recommended lesson"), School->GetFocusedLessonId(), FName(TEXT("A3")));
	TestEqual(TEXT("which Continue starts"), School->GetContinueLessonId(), FName(TEXT("A3")));
	TestEqual(TEXT("A3's status"), School->GetDetailStatusText().ToString(), FString(TEXT("NEW")));
	School->FocusLesson(TEXT("A4"));
	TestEqual(TEXT("A4's status names the missing prerequisite"), School->GetDetailStatusText().ToString(), FString(TEXT("LOCKED: pass A3 Speed control first")));
	TestEqual(TEXT("A4's needs"), School->GetDetailNeedsText().ToString(), FString(TEXT("A3 Speed control (not passed)")));
	TestFalse(TEXT("A4 cannot be started"), School->CanStartFocused());
	School->FocusLesson(TEXT("A6"));
	TestTrue(TEXT("A6's status: coming soon, toeside riding"), School->GetDetailStatusText().ToString().StartsWith(TEXT("COMING SOON"))
		&& School->GetDetailStatusText().ToString().Contains(TEXT("toeside riding")));
	School->FocusLesson(TEXT("A1"));
	TestEqual(TEXT("A1's status"), School->GetDetailStatusText().ToString(), FString(TEXT("PASSED: 3 stars")));
	TestTrue(TEXT("A1's best: three stars, one attempt, one pass, played today"), School->GetDetailBestText().ToString().Contains(TEXT("Best stars 3 / 3"))
		&& School->GetDetailBestText().ToString().Contains(TEXT("Attempts 1     Passes 1     Last played today")));
	TestEqual(TEXT("A1 needs nothing"), School->GetDetailNeedsText().ToString(), FString(TEXT("Nothing: start here.")));
	TestTrue(TEXT("A1 teaches its summary and its step"), School->GetDetailTeachText().ToString().StartsWith(LessonCatalog::Find(TEXT("A1"))->Summary.ToString())
		&& School->GetDetailTeachText().ToString().Contains(TEXT("1. Dive the kite to 45")));
	School->FocusLesson(TEXT("B2"));
	TestTrue(TEXT("B2's pass criterion in words"), School->GetDetailPassText().ToString().StartsWith(TEXT("Jump 1–2 m, 5 in a row, landed Clean or better.")));
	TestTrue(TEXT("and how to earn more stars"), School->GetDetailPassText().ToString().Contains(TEXT("2 stars: fewer assists, or Stomped landings. 3 stars: every assist off.")));
	TestEqual(TEXT("B2 never played"), School->GetDetailBestText().ToString(), FString(TEXT("Not played yet.")));
	TestFalse(TEXT("No lesson has a demonstration yet: Watch demo is hidden"), School->IsWatchDemoVisible());

	// Overall progress.
	const int32 MaxStars = LessonCatalog::GetAll().Num() * FLessonProgressBook::MaxStars;
	TestEqual(TEXT("Total stars and the trick book count"), School->GetOverallProgressText().ToString(),
		FString::Printf(TEXT("STARS  4 / %d        TRICK BOOK  0 tricks"), MaxStars));
	TestEqual(TEXT("Chapter A: two of seven passed"), School->GetChapterProgressText(TEXT("A")).ToString(), FString(TEXT("29%")));
	TestEqual(TEXT("Chapter B: none"), School->GetChapterProgressText(TEXT("B")).ToString(), FString(TEXT("0%")));
	TestTrue(TEXT("Chapter C has no lessons yet: no figure"), School->GetChapterProgressText(TEXT("C")).IsEmpty());

	// The words.
	TestEqual(TEXT("Six chapters"), SchoolMenuText::GetChapters().Num(), 6);
	TestTrue(TEXT("A to F"), SchoolMenuText::GetChapters().Num() == 6 && SchoolMenuText::GetChapters()[0].Id == FName(TEXT("A"))
		&& SchoolMenuText::GetChapters()[5].Id == FName(TEXT("F")));
	const FDateTime Now(2026, 10, 3, 9, 0, 0);
	TestEqual(TEXT("Last played: today"), SchoolMenuText::FormatLastPlayed(FDateTime(2026, 10, 3, 1, 0, 0), Now).ToString(), FString(TEXT("today")));
	TestEqual(TEXT("yesterday"), SchoolMenuText::FormatLastPlayed(FDateTime(2026, 10, 2, 23, 0, 0), Now).ToString(), FString(TEXT("yesterday")));
	TestEqual(TEXT("3 days ago"), SchoolMenuText::FormatLastPlayed(FDateTime(2026, 9, 30, 12, 0, 0), Now).ToString(), FString(TEXT("3 days ago")));
	TestEqual(TEXT("an older date"), SchoolMenuText::FormatLastPlayed(FDateTime(2026, 9, 1, 12, 0, 0), Now).ToString(), FString(TEXT("2026-09-01")));
	TestEqual(TEXT("A height"), SchoolMenuText::FormatValue(ELessonMetric::JumpHeight, 1.64f).ToString(), FString(TEXT("1.6 m")));
	TestEqual(TEXT("A landing grade"), SchoolMenuText::FormatValue(ELessonMetric::LandingGrade, 1.0f).ToString(), FString(TEXT("Clean")));
	TestEqual(TEXT("A speed in knots"), SchoolMenuText::FormatValue(ELessonMetric::SpeedHeld, 15.0f * 0.5144f).ToString(), FString(TEXT("15 kn")));
	return true;
}

// Keyboard and gamepad: up and down go through CONTINUE, the chapter rows and the run choices;
// left and right walk along a chapter's tiles and change the wind and the assists.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolMenuNavigation, "KiteSurf.School.MenuNavigation", SchoolMenuTest::Flags)

bool FKiteSurfSchoolMenuNavigation::RunTest(const FString& Parameters)
{
	using namespace SchoolMenuTest;
	FMenuWorldFixture Fx;
	ULessonSubsystem* Lessons = MakeMenuLessons();
	PassLessons(Lessons, { TEXT("A1"), TEXT("A2"), TEXT("A3"), TEXT("A4"), TEXT("A5") });
	UKiteSurfSchoolWidget* School = Fx.MakeSchool(Lessons);
	if (!TestNotNull(TEXT("Lesson menu created"), School))
	{
		return false;
	}
	FKiteMenuNavigator& Nav = School->GetNavigator();
	// CONTINUE, rows A and B (C to F have no lessons yet), WIND, ASSISTS, START, RESET PROGRESS, BACK.
	TestEqual(TEXT("Eight items"), Nav.Num(), 8);
	TestTrue(TEXT("in that order"), School->GetContinueItem() == 0 && School->GetChapterItem(TEXT("A")) == 1 && School->GetChapterItem(TEXT("B")) == 2
		&& School->GetWindItem() == 3 && School->GetAssistsItem() == 4 && School->GetStartItem() == 5 && School->GetResetItem() == 6 && School->GetBackItem() == 7);
	TestEqual(TEXT("No row for a chapter without lessons"), School->GetChapterItem(TEXT("C")), INDEX_NONE);
	TestEqual(TEXT("Opens on CONTINUE"), Nav.GetSelected(), School->GetContinueItem());
	// A6 needs toeside riding, so the first unlocked lesson without a star is A7.
	TestEqual(TEXT("focused on the recommended lesson"), School->GetFocusedLessonId(), FName(TEXT("A7")));

	Nav.HandleKey(EKeys::Down);
	TestEqual(TEXT("Down: chapter A's row"), Nav.GetSelected(), School->GetChapterItem(TEXT("A")));
	TestEqual(TEXT("still on A7"), School->GetFocusedLessonId(), FName(TEXT("A7")));
	Nav.HandleKey(EKeys::Right);
	TestEqual(TEXT("Right at the end of the row stays on A7"), School->GetFocusedLessonId(), FName(TEXT("A7")));
	Nav.HandleKey(EKeys::Left);
	TestEqual(TEXT("Left: A6"), School->GetFocusedLessonId(), FName(TEXT("A6")));
	Nav.HandleKey(EKeys::Gamepad_DPad_Left);
	TestEqual(TEXT("D-pad left: A5"), School->GetFocusedLessonId(), FName(TEXT("A5")));
	Nav.HandleKey(EKeys::Gamepad_LeftStick_Down);
	TestEqual(TEXT("Stick down: chapter B's row"), Nav.GetSelected(), School->GetChapterItem(TEXT("B")));
	TestEqual(TEXT("in the same column: B5"), School->GetFocusedLessonId(), FName(TEXT("B5")));
	Nav.HandleKey(EKeys::Gamepad_LeftStick_Left);
	Nav.HandleKey(EKeys::Gamepad_DPad_Left);
	Nav.HandleKey(EKeys::Left);
	Nav.HandleKey(EKeys::Left);
	Nav.HandleKey(EKeys::Left);
	TestEqual(TEXT("Left to the start of the row: B1"), School->GetFocusedLessonId(), FName(TEXT("B1")));
	Nav.HandleKey(EKeys::Gamepad_DPad_Up);
	TestEqual(TEXT("D-pad up: chapter A's row"), Nav.GetSelected(), School->GetChapterItem(TEXT("A")));
	TestEqual(TEXT("in column one: A1"), School->GetFocusedLessonId(), FName(TEXT("A1")));
	Nav.HandleKey(EKeys::Up);
	TestEqual(TEXT("Up: CONTINUE"), Nav.GetSelected(), School->GetContinueItem());
	TestEqual(TEXT("the detail panel keeps A1"), School->GetFocusedLessonId(), FName(TEXT("A1")));
	Nav.HandleKey(EKeys::Up);
	TestEqual(TEXT("Up from the top wraps to BACK"), Nav.GetSelected(), School->GetBackItem());

	// Wind for a rerun: from the lesson's own, up to 10 kn more.
	School->FocusLesson(TEXT("A2"));
	const float LessonWind = LessonCatalog::Find(TEXT("A2"))->Setup.WindKnots;
	TestEqual(TEXT("The run starts at the lesson's wind (kn)"), School->GetRunWindKnots(), LessonWind);
	Nav.Select(School->GetWindItem());
	Nav.HandleKey(EKeys::Right);
	Nav.HandleKey(EKeys::Gamepad_DPad_Right);
	Nav.HandleKey(EKeys::Right);
	TestEqual(TEXT("Right adds a knot each press (kn)"), School->GetRunWindKnots(), LessonWind + 3.0f);
	TestEqual(TEXT("which the run takes (kn)"), School->GetRunOptions().WindKnots, LessonWind + 3.0f);
	for (int32 I = 0; I < 5; ++I)
	{
		Nav.HandleKey(EKeys::Left);
	}
	TestEqual(TEXT("Left never goes below the lesson's wind (kn)"), School->GetRunWindKnots(), LessonWind);
	TestEqual(TEXT("and at the lesson's wind the run has no override"), School->GetRunOptions().WindKnots, 0.0f);
	for (int32 I = 0; I < 20; ++I)
	{
		Nav.HandleKey(EKeys::Right);
	}
	TestEqual(TEXT("Right stops 10 kn above the lesson's wind (kn)"), School->GetRunWindKnots(), LessonWind + UKiteSurfSchoolWidget::MaxExtraWindKnots);

	// Assists for a rerun: A2 has auto-park and auto-edge.
	Nav.HandleKey(EKeys::Down);
	TestEqual(TEXT("Down: ASSISTS"), Nav.GetSelected(), School->GetAssistsItem());
	TestEqual(TEXT("Four choices: the lesson's, without each, all off"), School->GetAssistChoiceCount(), 4);
	TestEqual(TEXT("The lesson's"), School->GetAssistChoiceText().ToString(), FString(TEXT("LESSON'S: auto-park, auto-edge")));
	TestFalse(TEXT("which is no override"), School->GetRunOptions().bOverrideAssists);
	Nav.HandleKey(EKeys::Right);
	TestEqual(TEXT("Right: without auto-park"), School->GetAssistChoiceText().ToString(), FString(TEXT("WITHOUT auto-park")));
	TestTrue(TEXT("the run rides with auto-edge only"), School->GetRunOptions().bOverrideAssists && !School->GetRunOptions().Assists.bAutoPark
		&& School->GetRunOptions().Assists.bAutoEdge && School->GetRunOptions().Assists.CountOn() == 1);
	Nav.HandleKey(EKeys::Gamepad_FaceButton_Bottom);
	TestEqual(TEXT("Accept steps on: without auto-edge"), School->GetAssistChoiceText().ToString(), FString(TEXT("WITHOUT auto-edge")));
	Nav.HandleKey(EKeys::Right);
	TestEqual(TEXT("All off"), School->GetAssistChoiceText().ToString(), FString(TEXT("ALL OFF")));
	TestTrue(TEXT("every assist off"), School->GetRunOptions().bOverrideAssists && School->GetRunOptions().Assists.CountOn() == 0);
	Nav.HandleKey(EKeys::Right);
	TestEqual(TEXT("and round to the lesson's"), School->GetAssistChoice(), 0);
	Nav.HandleKey(EKeys::Left);
	TestEqual(TEXT("Left goes back round to all off"), School->GetAssistChoiceText().ToString(), FString(TEXT("ALL OFF")));

	// Focusing another lesson puts its own choices back.
	School->FocusLesson(TEXT("A7"));
	TestTrue(TEXT("A new lesson: its own wind and assists"), School->GetAssistChoice() == 0
		&& School->GetRunWindKnots() == LessonCatalog::Find(TEXT("A7"))->Setup.WindKnots);
	TestEqual(TEXT("A7 has no assists: one choice"), School->GetAssistChoiceCount(), 1);
	return true;
}

// A locked lesson (prerequisite without a star) or one whose feature is not built cannot be started
// from the menu; an unlocked one can.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolMenuLockedCannotStart, "KiteSurf.School.MenuLockedCannotStart", SchoolMenuTest::Flags)

bool FKiteSurfSchoolMenuLockedCannotStart::RunTest(const FString& Parameters)
{
	using namespace SchoolMenuTest;
	const FMenuSettingsGuard Settings;
	FMenuWorldFixture Fx;
	ULessonSubsystem* Lessons = MakeMenuLessons();
	UKiteSurfSchoolWidget* School = Fx.MakeSchool(Lessons);
	if (!TestNotNull(TEXT("Lesson menu created"), School))
	{
		return false;
	}
	FKiteMenuNavigator& Nav = School->GetNavigator();

	School->FocusLesson(TEXT("B2"));
	TestFalse(TEXT("B2 is locked on a fresh save"), School->CanStartFocused());
	TestFalse(TEXT("START refuses it"), School->StartFocusedLesson());
	TestEqual(TEXT("Nothing is pending"), Lessons->GetPendingLessonId(), FName());

	// Accept on a tile starts it: not when it is locked.
	Nav.Select(School->GetChapterItem(TEXT("B")));
	TestEqual(TEXT("Chapter B's row keeps B2 in focus"), School->GetFocusedLessonId(), FName(TEXT("B2")));
	Nav.HandleKey(EKeys::Enter);
	TestEqual(TEXT("Accept on locked B2 starts nothing"), Lessons->GetPendingLessonId(), FName());
	Nav.HandleKey(EKeys::Left);
	TestEqual(TEXT("Left: B1"), School->GetFocusedLessonId(), FName(TEXT("B1")));
	Nav.HandleKey(EKeys::Gamepad_FaceButton_Bottom);
	TestEqual(TEXT("Accept on locked B1 starts nothing either"), Lessons->GetPendingLessonId(), FName());
	Nav.Select(School->GetStartItem());
	Nav.HandleKey(EKeys::Gamepad_FaceButton_Bottom);
	TestEqual(TEXT("nor does START"), Lessons->GetPendingLessonId(), FName());

	// Coming soon: locked whatever the prerequisites say.
	PassLessons(Lessons, { TEXT("A1"), TEXT("A2"), TEXT("A3"), TEXT("A4"), TEXT("A5") });
	School->Refresh();
	School->FocusLesson(TEXT("A6"));
	TestFalse(TEXT("A6 waits for toeside riding"), School->StartFocusedLesson());
	TestEqual(TEXT("Still nothing pending"), Lessons->GetPendingLessonId(), FName());

	// Unlocked: it starts.
	School->FocusLesson(TEXT("B1"));
	TestTrue(TEXT("B1 is unlocked after A4"), School->CanStartFocused());
	Nav.Select(School->GetChapterItem(TEXT("B")));
	Nav.HandleKey(EKeys::SpaceBar);
	TestEqual(TEXT("Accept on its tile starts B1"), Lessons->GetPendingLessonId(), FName(TEXT("B1")));
	Lessons->ClearPendingLesson();
	Settings.Check(*this);
	return true;
}

// CONTINUE starts the recommended lesson as the lesson sets it; a rerun with more wind and fewer
// assists carries its options to the lesson's director.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolMenuContinueAndRerunOptions, "KiteSurf.School.MenuContinueAndRerunOptions", SchoolMenuTest::Flags)

bool FKiteSurfSchoolMenuContinueAndRerunOptions::RunTest(const FString& Parameters)
{
	using namespace SchoolMenuTest;
	const FMenuSettingsGuard Settings;
	FMenuWorldFixture Fx;
	ULessonSubsystem* Lessons = MakeMenuLessons();
	PassLessons(Lessons, { TEXT("A1"), TEXT("A2") });
	UKiteSurfSchoolWidget* School = Fx.MakeSchool(Lessons);
	if (!TestNotNull(TEXT("Lesson menu created"), School))
	{
		return false;
	}
	TestEqual(TEXT("Continue goes to A3"), School->GetContinueLessonId(), FName(TEXT("A3")));
	FKiteMenuNavigator& Nav = School->GetNavigator();
	TestEqual(TEXT("The menu opens on CONTINUE"), Nav.GetSelected(), School->GetContinueItem());
	Nav.HandleKey(EKeys::Enter);
	TestEqual(TEXT("Accept starts A3"), Lessons->GetPendingLessonId(), FName(TEXT("A3")));
	TestTrue(TEXT("as the lesson sets it"), Lessons->GetPendingRunOptions().WindKnots == 0.0f && !Lessons->GetPendingRunOptions().bOverrideAssists);
	Lessons->ClearPendingLesson();

	// A rerun of A2 with 4 kn more and every assist off.
	UKiteSurfSchoolWidget* Rerun = Fx.MakeSchool(Lessons);
	Rerun->FocusLesson(TEXT("A2"));
	const float LessonWind = LessonCatalog::Find(TEXT("A2"))->Setup.WindKnots;
	Rerun->SetRunWindKnots(LessonWind + 4.0f);
	Rerun->StepAssistChoice(-1); // round to "all off"
	TestEqual(TEXT("All off is one step back"), Rerun->GetAssistChoiceText().ToString(), FString(TEXT("ALL OFF")));
	TestTrue(TEXT("The rerun of a passed lesson starts"), Rerun->StartFocusedLesson());
	TestEqual(TEXT("A2 is pending"), Lessons->GetPendingLessonId(), FName(TEXT("A2")));
	TestEqual(TEXT("with the wind (kn)"), Lessons->GetPendingRunOptions().WindKnots, LessonWind + 4.0f);
	TestTrue(TEXT("and no assists"), Lessons->GetPendingRunOptions().bOverrideAssists && Lessons->GetPendingRunOptions().Assists.CountOn() == 0);

	// The ride starts it: the director rides with those options.
	AKiteRiderPawn* Rider = Fx.SpawnRider();
	if (!TestNotNull(TEXT("Rider spawned"), Rider))
	{
		return false;
	}
	ALessonDirector* Director = Lessons->StartPendingLesson(Rider);
	if (!TestNotNull(TEXT("The pending rerun starts on the rider"), Director))
	{
		return false;
	}
	TestEqual(TEXT("A2 runs"), Director->GetLessonId(), FName(TEXT("A2")));
	TestEqual(TEXT("in the chosen wind (kn)"), Director->GetWindKnots(), LessonWind + 4.0f);
	TestEqual(TEXT("with every assist off"), Director->GetUsedAssists().CountOn(), 0);
	TestFalse(TEXT("so the rider has no auto-edge"), Rider->GetBoardMovement()->bAutoEdge);
	TestTrue(TEXT("The options are consumed with the pending lesson"), Lessons->GetPendingLessonId().IsNone()
		&& Lessons->GetPendingRunOptions().WindKnots == 0.0f && !Lessons->GetPendingRunOptions().bOverrideAssists);
	TestTrue(TEXT("Retry keeps them"), Director->Retry() && Director->GetWindKnots() == LessonWind + 4.0f && Director->GetUsedAssists().CountOn() == 0);
	Director->ExitToFreeRide();
	Settings.Check(*this);
	return true;
}

// Rerunning a passed lesson never lowers its best stars; the tile and the detail panel keep them.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolMenuRerunKeepsBestStars, "KiteSurf.School.MenuRerunKeepsBestStars", SchoolMenuTest::Flags)

bool FKiteSurfSchoolMenuRerunKeepsBestStars::RunTest(const FString& Parameters)
{
	using namespace SchoolMenuTest;
	FMenuWorldFixture Fx;
	ULessonSubsystem* Lessons = MakeMenuLessons();
	PassLessons(Lessons, { TEXT("A1") }, 3);
	UKiteSurfSchoolWidget* School = Fx.MakeSchool(Lessons);
	if (!TestNotNull(TEXT("Lesson menu created"), School))
	{
		return false;
	}
	School->FocusLesson(TEXT("A1"));
	TestTrue(TEXT("Passed A1 can be started again"), School->StartFocusedLesson());
	TestEqual(TEXT("A1 is pending"), Lessons->GetPendingLessonId(), FName(TEXT("A1")));
	Lessons->ClearPendingLesson();

	// The rerun's results: a one-star pass, then a miss.
	TestFalse(TEXT("A one-star pass does not raise three stars"), Lessons->RecordLessonResult(TEXT("A1"), true, 1, NAN, false));
	Lessons->RecordLessonResult(TEXT("A1"), false, 0, NAN, false);

	UKiteSurfSchoolWidget* After = Fx.MakeSchool(Lessons);
	After->FocusLesson(TEXT("A1"));
	const FLessonListItem* A1 = After->FindTile(TEXT("A1"));
	TestTrue(TEXT("The tile keeps three stars"), A1 && A1->Stars == 3);
	TestEqual(TEXT("The status keeps three stars"), After->GetDetailStatusText().ToString(), FString(TEXT("PASSED: 3 stars")));
	TestTrue(TEXT("The best result: three stars, three attempts, two passes"), After->GetDetailBestText().ToString().Contains(TEXT("Best stars 3 / 3"))
		&& After->GetDetailBestText().ToString().Contains(TEXT("Attempts 3     Passes 2")));
	TestEqual(TEXT("Total stars still three"), Lessons->GetTotalStars(), 3);
	return true;
}

// RESET PROGRESS asks first; CANCEL keeps everything, RESET forgets the lessons but never the trick book.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolMenuResetConfirmKeepsTrickBook, "KiteSurf.School.MenuResetConfirmKeepsTrickBook", SchoolMenuTest::Flags)

bool FKiteSurfSchoolMenuResetConfirmKeepsTrickBook::RunTest(const FString& Parameters)
{
	using namespace SchoolMenuTest;
	const FMenuSettingsGuard Settings;
	FMenuWorldFixture Fx;

	UKiteSurfGameInstance* GI = NewObject<UKiteSurfGameInstance>();
	FJumpRecord Record;
	Record.FamilyKey = TEXT("HH|L|-|I0|S-0|G");
	Record.TrickName = TEXT("Back roll");
	Record.Grade = ELandingGrade::Clean;
	Record.Score.Total = 41.5f;
	Record.ApexHeightCm = 812.0f;
	Record.Outcome = EJumpOutcome::Landed;
	TestTrue(TEXT("A trick in the game instance's book"), GI->RecordTrickLanding(Record));
	ULessonSubsystem* Lessons = MakeMenuLessons(GI);
	PassLessons(Lessons, { TEXT("A1"), TEXT("A2") }, 2);

	UKiteSurfSchoolWidget* School = Fx.MakeSchool(Lessons);
	if (!TestNotNull(TEXT("Lesson menu created"), School))
	{
		return false;
	}
	TestTrue(TEXT("The overall progress counts the trick book"), School->GetOverallProgressText().ToString().EndsWith(TEXT("TRICK BOOK  1 trick")));
	FKiteMenuNavigator& Nav = School->GetNavigator();
	Nav.Select(School->GetResetItem());
	Nav.HandleKey(EKeys::Enter);
	TestTrue(TEXT("RESET PROGRESS asks first"), School->IsConfirmingReset());
	TestEqual(TEXT("Nothing is reset yet"), Lessons->GetProgress().Num(), 2);
	TestEqual(TEXT("The question has two items, RESET and CANCEL"), Nav.Num(), 2);
	TestEqual(TEXT("and starts on CANCEL"), Nav.GetSelected(), School->GetCancelResetItem());
	Nav.HandleKey(EKeys::Gamepad_FaceButton_Bottom);
	TestFalse(TEXT("CANCEL closes the question"), School->IsConfirmingReset());
	TestEqual(TEXT("and keeps the progress"), Lessons->GetProgress().Num(), 2);
	TestEqual(TEXT("back on RESET PROGRESS"), Nav.GetSelected(), School->GetResetItem());

	Nav.HandleKey(EKeys::Enter);
	Nav.HandleKey(EKeys::Up);
	TestEqual(TEXT("Up: RESET"), Nav.GetSelected(), School->GetConfirmResetItem());
	Nav.HandleKey(EKeys::Enter);
	TestFalse(TEXT("The question is answered"), School->IsConfirmingReset());
	TestEqual(TEXT("Every lesson result is gone"), Lessons->GetProgress().Num(), 0);
	TestEqual(TEXT("No stars"), Lessons->GetTotalStars(), 0);
	const FLessonListItem* A1 = School->FindTile(TEXT("A1"));
	const FLessonListItem* A2 = School->FindTile(TEXT("A2"));
	TestTrue(TEXT("The tiles show it: A1 new again, A2 locked"), A1 && A1->bNew && A1->Stars == 0 && A2 && A2->bLocked);
	TestEqual(TEXT("The trick book is kept"), GI->GetTrickBook().Num(), 1);
	TestTrue(TEXT("and still counted"), School->GetOverallProgressText().ToString().EndsWith(TEXT("TRICK BOOK  1 trick")));
	TestEqual(TEXT("The menu is back on CONTINUE"), Nav.GetSelected(), School->GetContinueItem());
	TestEqual(TEXT("which goes to A1"), School->GetFocusedLessonId(), FName(TEXT("A1")));
	TestEqual(TEXT("The full menu is back"), Nav.Num(), 8);
	Settings.Check(*this);
	return true;
}

// The main menu's SCHOOL opens the lesson menu in its place; BACK brings the main menu back, and a
// started lesson leaves for its map.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolMenuFromMainMenu, "KiteSurf.School.MenuFromMainMenu", SchoolMenuTest::Flags)

bool FKiteSurfSchoolMenuFromMainMenu::RunTest(const FString& Parameters)
{
	using namespace SchoolMenuTest;
	FMenuWorldFixture Fx;
	UKiteSurfMainMenuWidget* MainMenu = Fx.World ? CreateWidget<UKiteSurfMainMenuWidget>(Fx.World, UKiteSurfMainMenuWidget::StaticClass()) : nullptr;
	if (!TestNotNull(TEXT("Main menu created"), MainMenu))
	{
		return false;
	}
	FKiteMenuNavigator& Nav = MainMenu->GetNavigator();
	TestEqual(TEXT("PLAY, SCHOOL, SETTINGS, QUIT"), Nav.Num(), 4);
	Nav.HandleKey(EKeys::Down);
	Nav.HandleKey(EKeys::Enter);
	UKiteSurfSchoolWidget* School = MainMenu->ActiveSchoolWidget;
	if (!TestNotNull(TEXT("SCHOOL, the second item, opens the lesson menu"), School))
	{
		return false;
	}
	TestFalse(TEXT("the full-screen version"), School->bDuringRide);
	TestEqual(TEXT("in place of the main menu"), MainMenu->GetVisibility(), ESlateVisibility::Collapsed);
	School->GetNavigator().Select(School->GetBackItem());
	School->GetNavigator().HandleKey(EKeys::Enter);
	TestNull(TEXT("BACK closes it"), MainMenu->ActiveSchoolWidget.Get());
	TestEqual(TEXT("and shows the main menu"), MainMenu->GetVisibility(), ESlateVisibility::Visible);
	TestEqual(TEXT("on PLAY"), MainMenu->GetNavigator().GetSelected(), 0);

	MainMenu->OnSchoolClicked();
	ULessonSubsystem* Lessons = MakeMenuLessons();
	if (MainMenu->ActiveSchoolWidget)
	{
		MainMenu->ActiveSchoolWidget->SetLessonSubsystem(Lessons);
		TestTrue(TEXT("CONTINUE starts A1"), MainMenu->ActiveSchoolWidget->Continue());
	}
	TestEqual(TEXT("A1 waits for its map (travel is off in tests)"), Lessons->GetPendingLessonId(), FName(TEXT("A1")));
	TestNull(TEXT("The lesson menu has gone"), MainMenu->ActiveSchoolWidget.Get());
	Lessons->ClearPendingLesson();
	return true;
}

// The pause menu: SCHOOL in free ride; in a lesson RETRY LESSON, LESSON MENU and FREE RIDE, with the
// items above them where they were. RequestLessonMenu opens the lesson menu over the paused ride.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolMenuPauseEntries, "KiteSurf.School.MenuPauseEntries", SchoolMenuTest::Flags)

bool FKiteSurfSchoolMenuPauseEntries::RunTest(const FString& Parameters)
{
	using namespace SchoolMenuTest;
	FMenuWorldFixture Fx;
	UWorld* World = Fx.World;
	APlayerController* PC = World ? World->SpawnActor<APlayerController>() : nullptr;
	AKiteSurfHUD* HUD = World ? World->SpawnActor<AKiteSurfHUD>() : nullptr;
	AKiteRiderPawn* Rider = Fx.SpawnRider();
	if (!TestTrue(TEXT("Controller, HUD and rider spawned"), PC && HUD && Rider))
	{
		return false;
	}
	PC->SetPlayerState(World->SpawnActor<APlayerState>());
	HUD->PlayerOwner = PC;
	PC->MyHUD = HUD;
	PC->Possess(Rider);

	// Free ride.
	HUD->ShowPauseMenu();
	UKiteSurfPauseMenuWidget* Pause = HUD->GetActivePauseMenuWidget();
	if (!TestNotNull(TEXT("The pause menu opens"), Pause))
	{
		return false;
	}
	TestEqual(TEXT("Free ride: RESUME, RESTART, GEAR, SESSION, SCHOOL, SETTINGS, MAIN MENU, QUIT"), Pause->GetNavigator().Num(), 8);
	TestFalse(TEXT("No lesson items"), Pause->ShowsLessonItems());
	Pause->GetNavigator().Select(4);
	Pause->GetNavigator().HandleKey(EKeys::Enter);
	if (TestNotNull(TEXT("The fifth item, SCHOOL, opens the lesson menu"), Pause->ActiveSchoolWidget.Get()))
	{
		TestTrue(TEXT("over the ride"), Pause->ActiveSchoolWidget->bDuringRide);
		TestEqual(TEXT("in place of the pause menu"), Pause->GetVisibility(), ESlateVisibility::Collapsed);
		Pause->ActiveSchoolWidget->Close();
	}
	TestNull(TEXT("BACK returns to the pause menu"), Pause->ActiveSchoolWidget.Get());
	TestEqual(TEXT("which shows again"), Pause->GetVisibility(), ESlateVisibility::Visible);
	TestTrue(TEXT("The ride is still paused"), UGameplayStatics::IsGamePaused(World));
	HUD->HidePauseMenu();

	// A lesson.
	ALessonDirector* Director = ALessonDirector::StartInWorld(World, *LessonCatalog::Find(TEXT("A3")), Rider);
	if (!TestNotNull(TEXT("A3 runs in the ride"), Director))
	{
		return false;
	}
	ULessonSubsystem* Lessons = MakeMenuLessons();
	Director->SetLessonSubsystem(Lessons);
	Director->IntroSeconds = 0.0f;
	for (int32 Frame = 0; Frame < 30 && Director->GetPhase() != ELessonPhase::Step; ++Frame)
	{
		Rider->Tick(1.0f / 60.0f);
		Director->UpdateLesson(1.0f / 60.0f);
	}
	TestEqual(TEXT("The lesson is past its intro"), Director->GetPhase(), ELessonPhase::Step);

	HUD->ShowPauseMenu();
	Pause = HUD->GetActivePauseMenuWidget();
	if (!TestNotNull(TEXT("The pause menu opens in the lesson"), Pause))
	{
		return false;
	}
	FKiteMenuNavigator& Nav = Pause->GetNavigator();
	TestTrue(TEXT("It shows the lesson items"), Pause->ShowsLessonItems());
	TestEqual(TEXT("RESUME, RESTART, GEAR, SESSION, RETRY LESSON, LESSON MENU, FREE RIDE, SETTINGS, MAIN MENU, QUIT"), Nav.Num(), 10);
	Nav.Select(2);
	Nav.HandleKey(EKeys::Enter);
	TestNotNull(TEXT("GEAR is still the third item"), Pause->ActiveGearWidget.Get());
	if (Pause->ActiveGearWidget)
	{
		Pause->ActiveGearWidget->Cancel();
	}

	Pause->GetNavigator().Select(5);
	Pause->GetNavigator().HandleKey(EKeys::Enter);
	if (TestNotNull(TEXT("LESSON MENU opens the lesson menu"), Pause->ActiveSchoolWidget.Get()))
	{
		TestTrue(TEXT("over the ride"), Pause->ActiveSchoolWidget->bDuringRide);
		Pause->ActiveSchoolWidget->Close();
	}

	Pause->GetNavigator().Select(4);
	Pause->GetNavigator().HandleKey(EKeys::Enter);
	TestTrue(TEXT("RETRY LESSON starts A3 again from its set-up"), IsValid(Director) && Director->GetLessonId() == FName(TEXT("A3"))
		&& Director->GetPhase() == ELessonPhase::Intro);
	TestFalse(TEXT("and resumes the ride"), UGameplayStatics::IsGamePaused(World));

	// The result card's "Lesson menu": the pause menu is opened, then the lesson menu over it.
	UKiteSurfSchoolWidget* FromHook = ULessonSubsystem::OpenLessonMenuInWorld(World);
	if (TestNotNull(TEXT("RequestLessonMenu opens the lesson menu in the ride"), FromHook))
	{
		TestTrue(TEXT("over the paused ride"), FromHook->bDuringRide && UGameplayStatics::IsGamePaused(World));
		TestTrue(TEXT("from the pause menu"), HUD->GetActivePauseMenuWidget() && HUD->GetActivePauseMenuWidget()->ActiveSchoolWidget == FromHook);
		FromHook->Close();
	}

	Pause = HUD->GetActivePauseMenuWidget();
	if (TestNotNull(TEXT("The pause menu is back"), Pause))
	{
		Pause->GetNavigator().Select(6);
		Pause->GetNavigator().HandleKey(EKeys::Enter);
	}
	TestTrue(TEXT("FREE RIDE ends the lesson"), !IsValid(Director) || !Director->IsRunning());
	TestFalse(TEXT("and resumes the ride"), UGameplayStatics::IsGamePaused(World));
	HUD->ShowPauseMenu();
	TestEqual(TEXT("Back in free ride the pause menu has SCHOOL again"), HUD->GetActivePauseMenuWidget() ? HUD->GetActivePauseMenuWidget()->GetNavigator().Num() : 0, 8);
	HUD->HidePauseMenu();
	return true;
}

// The result card's "Lesson menu" (ULessonSubsystem::RequestLessonMenu, S4) reaches the lesson menu: the
// subsystem on a real game instance binds OnLessonMenuRequested, and in a ride it opens the lesson
// menu over the paused game with that game instance's progress.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolMenuAnswersLessonMenuRequest, "KiteSurf.School.MenuAnswersLessonMenuRequest", SchoolMenuTest::Flags)

bool FKiteSurfSchoolMenuAnswersLessonMenuRequest::RunTest(const FString& Parameters)
{
	using namespace SchoolMenuTest;
	const FMenuSettingsGuard Settings;
	const float VolumeBefore = FApp::GetVolumeMultiplier(); // Init applies the Settings slot's volume

	TestFalse(TEXT("A subsystem made without Initialize has nothing bound"), MakeMenuLessons()->RequestLessonMenu());

	// A game instance initialised as a standalone game does it: Init makes the subsystems, then reads the Settings slot.
	UKiteSurfGameInstance* GI = NewObject<UKiteSurfGameInstance>(GEngine);
	GI->InitializeStandalone();
	UWorld* World = GI->GetWorld();
	ULessonSubsystem* Lessons = GI->GetSubsystem<ULessonSubsystem>();
	if (TestNotNull(TEXT("The game instance has the lesson subsystem"), Lessons) && TestNotNull(TEXT("and a world"), World))
	{
		Lessons->SetWriteToDisk(false);
		Lessons->SetTravelEnabled(false);
		TestTrue(TEXT("Initialize binds the lesson menu to the request"), Lessons->OnLessonMenuRequested.IsBound());

		APlayerController* PC = World->SpawnActor<APlayerController>();
		AKiteSurfHUD* HUD = World->SpawnActor<AKiteSurfHUD>();
		if (TestTrue(TEXT("Controller and HUD spawned"), PC && HUD))
		{
			PC->SetPlayerState(World->SpawnActor<APlayerState>());
			HUD->PlayerOwner = PC;
			PC->MyHUD = HUD;
			TestTrue(TEXT("The request is answered"), Lessons->RequestLessonMenu());
			UKiteSurfPauseMenuWidget* Pause = HUD->GetActivePauseMenuWidget();
			UKiteSurfSchoolWidget* School = Pause ? Pause->ActiveSchoolWidget.Get() : nullptr;
			if (TestNotNull(TEXT("by the lesson menu, opened from the pause menu"), School))
			{
				TestTrue(TEXT("over the paused ride"), School->bDuringRide && UGameplayStatics::IsGamePaused(World));
				TestEqual(TEXT("with the game instance's progress"), School->GetLessons(), Lessons);
				School->Close();
			}
			HUD->HidePauseMenu();
		}
	}
	GI->Shutdown();
	if (World && GEngine)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}
	FApp::SetVolumeMultiplier(VolumeBefore);
	Settings.Check(*this);
	return true;
}

#endif
