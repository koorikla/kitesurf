#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "School/LessonCatalog.h"
#include "School/LessonProgress.h"
#include "School/LessonSubsystem.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/TrickBook.h"
#include "UI/KiteSurfGameInstance.h"
#include "UI/KiteSurfSaveGame.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests on the kite school's progress (S2, docs/tutorials.md 3.1 and 3.3): FLessonProgressBook,
// the unlock graph and the Continue rule over the real catalogue, the save round trip, old saves,
// and ULessonSubsystem on a game instance made here. Nothing here writes the player's own
// "Settings" slot: saves go through memory or the "SchoolProgressAutomationTest" slot (deleted
// afterwards), and the subsystem test turns the subsystem's disk writes off and checks that the
// Settings file is unchanged.

// Named, not anonymous, so a unity build cannot merge these with another file's helpers.
namespace SchoolProgressTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	const FString TestSlot = TEXT("SchoolProgressAutomationTest");

	FDateTime Day(int32 DayOfMonth, int32 Hour = 12)
	{
		return FDateTime(2026, 9, DayOfMonth, Hour, 0, 0);
	}

	bool RecordsEqual(const FLessonRecord& A, const FLessonRecord& B)
	{
		return A.LessonId == B.LessonId && A.BestStars == B.BestStars && A.bHasBestValue == B.bHasBestValue && A.BestValue == B.BestValue
			&& A.BestValueMetric == B.BestValueMetric && A.Attempts == B.Attempts && A.Passes == B.Passes
			&& A.FirstPassedUtc == B.FirstPassedUtc && A.LastPlayedUtc == B.LastPlayedUtc && A.bPassedNoAssists == B.bPassedNoAssists;
	}

	bool BooksEqual(const FLessonProgressBook& A, const FLessonProgressBook& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (!RecordsEqual(A.GetEntries()[Index], B.GetEntries()[Index]))
			{
				return false;
			}
		}
		return true;
	}

	/** One passing attempt on each lesson given, with the given stars. */
	void PassAll(FLessonProgressBook& Book, const TArray<const TCHAR*>& Ids, int32 Stars = 1, int32 DayOfMonth = 1)
	{
		for (const TCHAR* Id : Ids)
		{
			Book.RecordAttempt(Id, true, Stars, NAN, false, Day(DayOfMonth));
		}
	}

	/** A book with three lessons played: varied stars, values, attempts and dates. */
	FLessonProgressBook MakeBook()
	{
		FLessonProgressBook Book;
		Book.RecordAttempt(TEXT("A1"), false, 0, 3.0f, false, Day(1, 9));
		Book.RecordAttempt(TEXT("A1"), true, 1, 6.0f, false, Day(1, 10));
		Book.RecordAttempt(TEXT("A2"), true, 3, 2.5f, true, Day(2, 11));
		Book.RecordAttempt(TEXT("B1"), true, 2, 0.75f, false, Day(3, 12), ELessonMetric::JumpHeight);
		return Book;
	}

	FTrickBook MakeTrickBook()
	{
		FJumpRecord Record;
		Record.FamilyKey = TEXT("HH|L|-|I0|S-0|G");
		Record.TrickName = TEXT("Back roll");
		Record.Grade = ELandingGrade::Clean;
		Record.Score.Total = 41.5f;
		Record.ApexHeightCm = 812.0f;
		Record.Outcome = EJumpOutcome::Landed;
		FTrickBook Book;
		Book.RecordLanding(Record, ETrickBoardCategory::TwinTip, Day(4));
		return Book;
	}

	bool TrickBooksEqual(const FTrickBook& A, const FTrickBook& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			const FTrickBookEntry& X = A.GetEntries()[Index];
			const FTrickBookEntry& Y = B.GetEntries()[Index];
			if (X.FamilyKey != Y.FamilyKey || X.TimesLanded != Y.TimesLanded || X.BestScore != Y.BestScore || X.FirstLandedUtc != Y.FirstLandedUtc)
			{
				return false;
			}
		}
		return true;
	}

	/** Archive that writes like UGameplayStatics::SaveGameToMemory but leaves one property out: how a save from an older build looks. */
	class FSkipPropertyArchive : public FObjectAndNameAsStringProxyArchive
	{
	public:
		FSkipPropertyArchive(FArchive& InInner, FName InSkipped)
			: FObjectAndNameAsStringProxyArchive(InInner, false)
			, Skipped(InSkipped)
		{
		}

		virtual bool ShouldSkipProperty(const FProperty* InProperty) const override
		{
			return (InProperty && InProperty->GetFName() == Skipped) || FObjectAndNameAsStringProxyArchive::ShouldSkipProperty(InProperty);
		}

	private:
		FName Skipped;
	};

	/** The object part of a save game blob, written as SaveGameToMemory writes it, optionally without one property. */
	TArray<uint8> SerializeObject(USaveGame& Save, FName Skipped = NAME_None)
	{
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes, true);
		if (Skipped.IsNone())
		{
			FObjectAndNameAsStringProxyArchive Ar(Writer, false);
			Save.Serialize(Ar);
		}
		else
		{
			FSkipPropertyArchive Ar(Writer, Skipped);
			Save.Serialize(Ar);
		}
		return Bytes;
	}

	UKiteSurfSaveGame* RoundTrip(UKiteSurfSaveGame* Save)
	{
		TArray<uint8> Bytes;
		if (!UGameplayStatics::SaveGameToMemory(Save, Bytes))
		{
			return nullptr;
		}
		return Cast<UKiteSurfSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	}

	/** The player's Settings save in this project's Saved folder. */
	FString SettingsSavePath()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), UKiteSurfSaveGame::DefaultSaveSlot + TEXT(".sav"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolProgressRecordsBests, "KiteSurf.School.ProgressRecordsBests", SchoolProgressTest::Flags)

bool FKiteSurfSchoolProgressRecordsBests::RunTest(const FString& Parameters)
{
	using namespace SchoolProgressTest;
	const FName B1(TEXT("B1"));

	FLessonProgressBook Book;
	TestNull(TEXT("A new book has no record of B1"), Book.Find(B1));
	TestEqual(TEXT("A new book gives B1 no stars"), Book.GetStars(B1), 0);

	// A failed attempt counts, but earns nothing, whatever stars it claims.
	TestFalse(TEXT("A failed attempt raises no stars"), Book.RecordAttempt(B1, false, 2, 0.3f, true, Day(1)));
	const FLessonRecord* R = Book.Find(B1);
	if (!TestNotNull(TEXT("A failed attempt makes a record"), R))
	{
		return false;
	}
	TestEqual(TEXT("Failed: one attempt"), R->Attempts, 1);
	TestEqual(TEXT("Failed: no pass"), R->Passes, 0);
	TestEqual(TEXT("Failed: no stars"), R->BestStars, 0);
	TestFalse(TEXT("Failed: no no-assists pass"), R->bPassedNoAssists);
	TestFalse(TEXT("Failed: not passed"), R->HasPassed());
	TestTrue(TEXT("Failed: first passed is unset"), R->FirstPassedUtc == FDateTime());
	TestTrue(TEXT("Failed: last played is the attempt"), R->LastPlayedUtc == Day(1));
	TestTrue(TEXT("Failed: its value is kept as the best so far (m)"), R->bHasBestValue && R->BestValue == 0.3f);
	TestEqual(TEXT("The value's metric is B1's pass metric from the catalogue"), R->BestValueMetric, LessonCatalog::Find(B1)->Pass.Metric);

	// First pass: one star, dates set.
	TestTrue(TEXT("The first pass raises the stars"), Book.RecordAttempt(B1, true, 1, 0.6f, false, Day(2)));
	R = Book.Find(B1);
	TestEqual(TEXT("Pass: two attempts"), R->Attempts, 2);
	TestEqual(TEXT("Pass: one pass"), R->Passes, 1);
	TestEqual(TEXT("Pass: one star"), R->BestStars, 1);
	TestTrue(TEXT("Pass: first passed is this attempt"), R->FirstPassedUtc == Day(2));
	TestTrue(TEXT("Pass: last played is this attempt"), R->LastPlayedUtc == Day(2));
	TestEqual(TEXT("Pass: a higher height is the best (m)"), R->BestValue, 0.6f);

	// A pass claiming zero stars is still worth one; more than three is three.
	TestTrue(TEXT("Three stars raise the best"), Book.RecordAttempt(B1, true, 7, 0.5f, true, Day(3)));
	R = Book.Find(B1);
	TestEqual(TEXT("Stars are clamped to three"), R->BestStars, 3);
	TestTrue(TEXT("A pass with every assist off is noted"), R->bPassedNoAssists);
	TestEqual(TEXT("A lower height does not replace the best (m)"), R->BestValue, 0.6f);

	// Stars only go up; attempts keep counting; last played moves, first passed does not.
	TestFalse(TEXT("A one-star pass after three stars raises nothing"), Book.RecordAttempt(B1, true, 1, 0.9f, false, Day(4)));
	TestFalse(TEXT("A failure after three stars raises nothing"), Book.RecordAttempt(B1, false, 0, NAN, false, Day(5)));
	R = Book.Find(B1);
	TestEqual(TEXT("Stars stay at three"), R->BestStars, 3);
	TestEqual(TEXT("Five attempts"), R->Attempts, 5);
	TestEqual(TEXT("Three passes"), R->Passes, 3);
	TestTrue(TEXT("First passed stays at the first pass"), R->FirstPassedUtc == Day(2));
	TestTrue(TEXT("Last played is the newest attempt"), R->LastPlayedUtc == Day(5));
	TestTrue(TEXT("The no-assists pass stays noted"), R->bPassedNoAssists);
	TestEqual(TEXT("A higher height on a one-star pass is the new best (m); NaN is no value"), R->BestValue, 0.9f);

	// A pass claiming no stars is still worth one.
	FLessonProgressBook ZeroStars;
	TestTrue(TEXT("A pass claiming zero stars raises the stars"), ZeroStars.RecordAttempt(TEXT("B3"), true, 0, NAN, false, Day(3)));
	TestEqual(TEXT("A pass claiming zero stars is worth one"), ZeroStars.GetStars(TEXT("B3")), 1);
	TestFalse(TEXT("No value reported: no best value"), ZeroStars.Find(TEXT("B3"))->bHasBestValue);

	// Lower is better for the landing grade (Stomped 0).
	Book.RecordAttempt(TEXT("B2"), true, 1, float(ELandingGrade::Sketchy), false, Day(6), ELessonMetric::LandingGrade);
	Book.RecordAttempt(TEXT("B2"), true, 1, float(ELandingGrade::Stomped), false, Day(7), ELessonMetric::LandingGrade);
	Book.RecordAttempt(TEXT("B2"), true, 1, float(ELandingGrade::Clean), false, Day(8), ELessonMetric::LandingGrade);
	TestEqual(TEXT("The best landing grade is the lowest index (Stomped)"), Book.Find(TEXT("B2"))->BestValue, float(ELandingGrade::Stomped));
	TestTrue(TEXT("The landing grade is a lower-is-better metric"), LessonProgress::IsLowerBetter(ELessonMetric::LandingGrade));
	TestFalse(TEXT("Jump height is a higher-is-better metric"), LessonProgress::IsLowerBetter(ELessonMetric::JumpHeight));

	// Totals and chapters.
	TestFalse(TEXT("An id of None is ignored"), Book.RecordAttempt(NAME_None, true, 3, 1.0f, true, Day(9)));
	TestEqual(TEXT("Two lessons recorded"), Book.Num(), 2);
	TestEqual(TEXT("Total stars: 3 on B1 and 1 on B2"), Book.TotalStars(), 4);
	const int32 ChapterB = LessonCatalog::GetAll().FilterByPredicate([](const FLessonDef& L) { return L.Chapter == FName(TEXT("B")); }).Num();
	TestEqual(TEXT("Chapter B has six lessons in the catalogue"), ChapterB, 6);
	TestNearlyEqual(TEXT("Chapter B completion: 2 of 6 passed"), Book.ChapterCompletion(TEXT("B")), 2.0f / 6.0f, 1e-6f);
	TestEqual(TEXT("Chapter A completion: none passed"), Book.ChapterCompletion(TEXT("A")), 0.0f);
	TestEqual(TEXT("A chapter with no lessons is at 0"), Book.ChapterCompletion(TEXT("Z")), 0.0f);
	Book.RecordAttempt(TEXT("A1"), false, 0, NAN, false, Day(10));
	TestEqual(TEXT("A failed lesson does not count towards its chapter"), Book.ChapterCompletion(TEXT("A")), 0.0f);

	// Loading sanitises.
	TArray<FLessonRecord> Loaded = Book.GetEntries();
	FLessonRecord Duplicate = Loaded[0];
	Duplicate.BestStars = 1;
	Loaded.Add(Duplicate);
	FLessonRecord NoId;
	NoId.Passes = 1;
	NoId.BestStars = 2;
	Loaded.Add(NoId);
	FLessonRecord Bad;
	Bad.LessonId = TEXT("A3");
	Bad.BestStars = 3; // stars without a pass
	Bad.bPassedNoAssists = true;
	Bad.Attempts = -2;
	Loaded.Add(Bad);
	FLessonRecord TooMany;
	TooMany.LessonId = TEXT("A4");
	TooMany.BestStars = 9;
	TooMany.Passes = 2;
	TooMany.Attempts = 1;
	Loaded.Add(TooMany);
	FLessonProgressBook Reloaded;
	Reloaded.SetEntries(Loaded);
	TestEqual(TEXT("SetEntries drops the duplicate and the record with no id"), Reloaded.Num(), Book.Num() + 2);
	TestEqual(TEXT("SetEntries keeps the first of a duplicate"), Reloaded.GetStars(B1), 3);
	TestEqual(TEXT("SetEntries gives a record with no pass no stars"), Reloaded.GetStars(TEXT("A3")), 0);
	TestFalse(TEXT("SetEntries gives a record with no pass no no-assists pass"), Reloaded.Find(TEXT("A3"))->bPassedNoAssists);
	TestEqual(TEXT("SetEntries clamps negative attempts"), Reloaded.Find(TEXT("A3"))->Attempts, 0);
	TestEqual(TEXT("SetEntries clamps stars to three"), Reloaded.GetStars(TEXT("A4")), 3);
	TestEqual(TEXT("SetEntries makes attempts at least the passes"), Reloaded.Find(TEXT("A4"))->Attempts, 2);

	Book.Reset();
	TestEqual(TEXT("Reset empties the book"), Book.Num(), 0);
	TestEqual(TEXT("Reset leaves no stars"), Book.TotalStars(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolUnlockFollowsPrerequisites, "KiteSurf.School.UnlockFollowsPrerequisites", SchoolProgressTest::Flags)

bool FKiteSurfSchoolUnlockFollowsPrerequisites::RunTest(const FString& Parameters)
{
	using namespace SchoolProgressTest;
	auto Unlocked = [](const FLessonProgressBook& Book, const TCHAR* Id)
	{
		const FLessonDef* Lesson = LessonCatalog::Find(Id);
		return Lesson && LessonUnlock::IsUnlocked(*Lesson, Book);
	};

	// At the start only A1 is open.
	FLessonProgressBook Book;
	for (const FLessonDef& Lesson : LessonCatalog::GetAll())
	{
		const bool bExpected = Lesson.Id == FName(TEXT("A1"));
		TestTrue(*FString::Printf(TEXT("At the start, %s is %s"), *Lesson.Id.ToString(), bExpected ? TEXT("unlocked") : TEXT("locked")),
			LessonUnlock::IsUnlocked(Lesson, Book) == bExpected);
	}

	// One star on A1 opens A2 and nothing else.
	PassAll(Book, { TEXT("A1") });
	TestTrue(TEXT("A star on A1 unlocks A2"), Unlocked(Book, TEXT("A2")));
	TestFalse(TEXT("A star on A1 does not unlock A3"), Unlocked(Book, TEXT("A3")));
	TestTrue(TEXT("A passed lesson stays unlocked"), Unlocked(Book, TEXT("A1")));

	// B1 needs A4.
	PassAll(Book, { TEXT("A2"), TEXT("A3") });
	TestTrue(TEXT("A4 unlocks after A3"), Unlocked(Book, TEXT("A4")));
	TestFalse(TEXT("B1 is locked before A4 has a star"), Unlocked(Book, TEXT("B1")));
	Book.RecordAttempt(TEXT("A4"), false, 0, NAN, false, Day(2));
	TestFalse(TEXT("B1 stays locked after a failed A4 (no star)"), Unlocked(Book, TEXT("B1")));
	PassAll(Book, { TEXT("A4") });
	TestTrue(TEXT("B1 unlocks once A4 has a star"), Unlocked(Book, TEXT("B1")));
	TestTrue(TEXT("A5 unlocks once A4 has a star"), Unlocked(Book, TEXT("A5")));
	TestFalse(TEXT("B2 is still locked"), Unlocked(Book, TEXT("B2")));

	// B5 needs both B3 and A5.
	PassAll(Book, { TEXT("B1"), TEXT("B2"), TEXT("B3") });
	TestFalse(TEXT("B5 is locked with B3 but not A5"), Unlocked(Book, TEXT("B5")));
	PassAll(Book, { TEXT("A5") });
	TestTrue(TEXT("B5 unlocks with B3 and A5"), Unlocked(Book, TEXT("B5")));

	// Lessons whose feature is not built stay locked whatever the prerequisites.
	TestFalse(TEXT("Toeside riding is not built"), LessonCatalog::IsFeatureBuilt(ELessonFeature::ToesideRiding));
	TestFalse(TEXT("Grabs are not built"), LessonCatalog::IsFeatureBuilt(ELessonFeature::Grabs));
	TestEqual(TEXT("A5 has a star, A6's only prerequisite"), Book.GetStars(TEXT("A5")), 1);
	TestFalse(TEXT("A6 is locked while toeside riding is unavailable"), Unlocked(Book, TEXT("A6")));
	TestEqual(TEXT("B3 has a star, B4's only prerequisite"), Book.GetStars(TEXT("B3")), 1);
	TestFalse(TEXT("B4 is locked while grabs are unavailable"), Unlocked(Book, TEXT("B4")));
	TestTrue(TEXT("A7 (no feature needed) unlocks after A5"), Unlocked(Book, TEXT("A7")));

	// With every lesson starred, exactly the unavailable ones are locked.
	FLessonProgressBook All;
	for (const FLessonDef& Lesson : LessonCatalog::GetAll())
	{
		All.RecordAttempt(Lesson.Id, true, 1, NAN, false, Day(3));
	}
	for (const FLessonDef& Lesson : LessonCatalog::GetAll())
	{
		TestTrue(*FString::Printf(TEXT("With every prerequisite passed, %s is unlocked exactly when it is available"), *Lesson.Id.ToString()),
			LessonUnlock::IsUnlocked(Lesson, All) == LessonCatalog::IsAvailable(Lesson));
	}

	// A prerequisite the book has never seen counts as no stars.
	FLessonDef Orphan;
	Orphan.Id = TEXT("Z1");
	Orphan.Requires = { TEXT("Z0") };
	TestFalse(TEXT("A lesson needing an unknown lesson is locked"), LessonUnlock::IsUnlocked(Orphan, All));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolRecommendedNext, "KiteSurf.School.RecommendedNextFollowsRule", SchoolProgressTest::Flags)

bool FKiteSurfSchoolRecommendedNext::RunTest(const FString& Parameters)
{
	using namespace SchoolProgressTest;
	FLessonProgressBook Book;
	TestEqual(TEXT("At the start Continue goes to A1"), LessonUnlock::RecommendedNext(Book), FName(TEXT("A1")));

	Book.RecordAttempt(TEXT("A1"), false, 0, NAN, false, Day(1));
	TestEqual(TEXT("A failed A1 is still the next lesson"), LessonUnlock::RecommendedNext(Book), FName(TEXT("A1")));

	PassAll(Book, { TEXT("A1") });
	TestEqual(TEXT("After A1, A2 (unlocked, no star)"), LessonUnlock::RecommendedNext(Book), FName(TEXT("A2")));

	PassAll(Book, { TEXT("A2"), TEXT("A3"), TEXT("A4") });
	TestEqual(TEXT("After A4, A5 comes before B1 (lowest-numbered: catalogue order)"), LessonUnlock::RecommendedNext(Book), FName(TEXT("A5")));

	PassAll(Book, { TEXT("A5") });
	TestEqual(TEXT("After A5, A7 (A6 is locked: toeside is not built)"), LessonUnlock::RecommendedNext(Book), FName(TEXT("A7")));

	PassAll(Book, { TEXT("A7") });
	TestEqual(TEXT("With chapter A done, B1"), LessonUnlock::RecommendedNext(Book), FName(TEXT("B1")));

	// Every unlocked lesson starred: the one with the fewest stars, the earliest on a tie.
	PassAll(Book, { TEXT("B1"), TEXT("B2"), TEXT("B3"), TEXT("B5"), TEXT("B6") });
	TestEqual(TEXT("Every unlocked lesson at one star: the earliest, A1"), LessonUnlock::RecommendedNext(Book), FName(TEXT("A1")));
	PassAll(Book, { TEXT("A1"), TEXT("A2"), TEXT("A3"), TEXT("A4"), TEXT("A5"), TEXT("A7"), TEXT("B1"), TEXT("B2"), TEXT("B3"), TEXT("B5"), TEXT("B6") }, 3);
	Book.RecordAttempt(TEXT("B2"), true, 2, NAN, false, Day(2)); // stays at three
	FLessonProgressBook Mixed;
	Mixed.SetEntries(Book.GetEntries());
	TestEqual(TEXT("Everything at three stars: the first unlocked lesson"), LessonUnlock::RecommendedNext(Mixed), FName(TEXT("A1")));

	// Build a book with B3 and B5 at two stars and the rest at three.
	FLessonProgressBook Two;
	PassAll(Two, { TEXT("A1"), TEXT("A2"), TEXT("A3"), TEXT("A4"), TEXT("A5"), TEXT("A7"), TEXT("B1"), TEXT("B2"), TEXT("B6") }, 3);
	PassAll(Two, { TEXT("B5"), TEXT("B3") }, 2);
	TestEqual(TEXT("Fewest stars wins, the earliest in the catalogue on a tie (B3 before B5)"), LessonUnlock::RecommendedNext(Two), FName(TEXT("B3")));
	PassAll(Two, { TEXT("B3") }, 3);
	TestEqual(TEXT("B5 once B3 has three"), LessonUnlock::RecommendedNext(Two), FName(TEXT("B5")));

	// A starless unlocked lesson beats a lesson with fewer stars than the rest.
	FLessonProgressBook Gap;
	PassAll(Gap, { TEXT("A1"), TEXT("A2") }, 3);
	PassAll(Gap, { TEXT("A3") }, 1);
	TestEqual(TEXT("A4 (no star) comes before A3 (one star)"), LessonUnlock::RecommendedNext(Gap), FName(TEXT("A4")));

	// No lesson unlocked: none.
	FLessonDef Locked;
	Locked.Id = TEXT("Z1");
	Locked.Requires = { TEXT("Z0") };
	TestEqual(TEXT("Nothing unlocked: no recommendation"), LessonUnlock::RecommendedNext(FLessonProgressBook(), TArray<FLessonDef>{ Locked }), FName(NAME_None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolProgressPersists, "KiteSurf.School.ProgressPersists", SchoolProgressTest::Flags)

bool FKiteSurfSchoolProgressPersists::RunTest(const FString& Parameters)
{
	using namespace SchoolProgressTest;
	const FLessonProgressBook Book = MakeBook();
	TestEqual(TEXT("The test book has three lessons"), Book.Num(), 3);

	// In memory, as UGameplayStatics writes and reads it.
	UKiteSurfSaveGame* Save = NewObject<UKiteSurfSaveGame>();
	Save->WindStrengthKnots = 27.0f;
	Save->TrickBook = MakeTrickBook();
	Save->LessonProgress = Book;
	UKiteSurfSaveGame* Loaded = RoundTrip(Save);
	if (!TestNotNull(TEXT("The save loads back from memory"), Loaded))
	{
		return false;
	}
	TestTrue(TEXT("Lesson progress survives the round trip, record for record"), BooksEqual(Loaded->LessonProgress, Book));
	const FLessonRecord* A1 = Loaded->LessonProgress.Find(TEXT("A1"));
	TestTrue(TEXT("A1's attempts, passes, stars, best value and dates are kept"), A1 && A1->Attempts == 2 && A1->Passes == 1 && A1->BestStars == 1
		&& A1->BestValue == 6.0f && A1->FirstPassedUtc == Day(1, 10) && A1->LastPlayedUtc == Day(1, 10));
	const FLessonRecord* A2 = Loaded->LessonProgress.Find(TEXT("A2"));
	TestTrue(TEXT("A2's three stars and no-assists pass are kept"), A2 && A2->BestStars == 3 && A2->bPassedNoAssists);
	TestTrue(TEXT("The trick book is kept next to the progress"), TrickBooksEqual(Loaded->TrickBook, Save->TrickBook));
	TestEqual(TEXT("The settings are kept next to the progress (kn)"), Loaded->WindStrengthKnots, 27.0f);

	// Through the subsystem's save hooks, as the game instance calls them.
	ULessonSubsystem* Lessons = NewObject<ULessonSubsystem>(NewObject<UKiteSurfGameInstance>());
	Lessons->SetWriteToDisk(false);
	Lessons->LoadFromSaveGame(*Loaded);
	TestTrue(TEXT("LoadFromSaveGame takes the progress"), BooksEqual(Lessons->GetProgress(), Book));
	TestEqual(TEXT("The subsystem reports the loaded stars"), Lessons->GetTotalStars(), 6);
	UKiteSurfSaveGame* Written = NewObject<UKiteSurfSaveGame>();
	Lessons->WriteToSaveGame(*Written);
	TestTrue(TEXT("WriteToSaveGame writes the progress"), BooksEqual(Written->LessonProgress, Book));

	// On disk, in a slot of the test's own.
	TestNotEqual(TEXT("The test slot is not the player's slot"), TestSlot, UKiteSurfSaveGame::DefaultSaveSlot);
	UGameplayStatics::DeleteGameInSlot(TestSlot, UKiteSurfSaveGame::DefaultUserIndex);
	TestTrue(TEXT("The save is written to the test slot"), Save->SaveSettings(TestSlot));
	const UKiteSurfSaveGame* FromDisk = UKiteSurfSaveGame::LoadOrCreateSettings(TestSlot);
	TestTrue(TEXT("Lesson progress survives the round trip through the test slot"), FromDisk && BooksEqual(FromDisk->LessonProgress, Book));
	UGameplayStatics::DeleteGameInSlot(TestSlot, UKiteSurfSaveGame::DefaultUserIndex);
	TestFalse(TEXT("The test slot is cleaned up"), UGameplayStatics::DoesSaveGameExist(TestSlot, UKiteSurfSaveGame::DefaultUserIndex));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolOldSaveLoads, "KiteSurf.School.OldSaveLoadsWithoutProgress", SchoolProgressTest::Flags)

bool FKiteSurfSchoolOldSaveLoads::RunTest(const FString& Parameters)
{
	using namespace SchoolProgressTest;
	const FName ProgressProperty = GET_MEMBER_NAME_CHECKED(UKiteSurfSaveGame, LessonProgress);

	UKiteSurfSaveGame* Save = NewObject<UKiteSurfSaveGame>();
	Save->WindStrengthKnots = 27.0f;
	Save->RiderCharacterIndex = 1;
	Save->bHaptics = false;
	Save->TrickBook = MakeTrickBook();
	Save->LessonProgress = MakeBook();
	TArray<uint8> Full;
	UGameplayStatics::SaveGameToMemory(Save, Full);

	// Rebuild it as a build before the kite school wrote it: the same header, the object without LessonProgress.
	const TArray<uint8> ObjectFull = SerializeObject(*Save);
	const TArray<uint8> ObjectOld = SerializeObject(*Save, ProgressProperty);
	const int32 HeaderBytes = Full.Num() - ObjectFull.Num();
	const bool bSplits = HeaderBytes > 0 && FMemory::Memcmp(Full.GetData() + HeaderBytes, ObjectFull.GetData(), ObjectFull.Num()) == 0;
	if (!TestTrue(TEXT("The save is a header followed by the object, as SaveGameToMemory writes it"), bSplits))
	{
		return false;
	}
	TestTrue(TEXT("Leaving LessonProgress out makes the object smaller (the progress is really gone)"), ObjectOld.Num() < ObjectFull.Num());
	TArray<uint8> Old(Full.GetData(), HeaderBytes);
	Old.Append(ObjectOld);

	UKiteSurfSaveGame* Loaded = Cast<UKiteSurfSaveGame>(UGameplayStatics::LoadGameFromMemory(Old));
	if (!TestNotNull(TEXT("A save without lesson progress loads"), Loaded))
	{
		return false;
	}
	TestEqual(TEXT("It loads with no lesson progress"), Loaded->LessonProgress.Num(), 0);
	TestTrue(TEXT("Its trick book is kept"), TrickBooksEqual(Loaded->TrickBook, Save->TrickBook));
	TestEqual(TEXT("Its wind is kept (kn)"), Loaded->WindStrengthKnots, 27.0f);
	TestEqual(TEXT("Its rider is kept"), Loaded->RiderCharacterIndex, 1);
	TestFalse(TEXT("Its haptics setting is kept"), Loaded->bHaptics);

	UKiteSurfSaveGame* LoadedFull = Cast<UKiteSurfSaveGame>(UGameplayStatics::LoadGameFromMemory(Full));
	TestTrue(TEXT("The control: the same save with the property loads with its progress"), LoadedFull && LoadedFull->LessonProgress.Num() == 3);

	// The subsystem loading the old save forgets what it had: no progress, A1 is next.
	ULessonSubsystem* Lessons = NewObject<ULessonSubsystem>(NewObject<UKiteSurfGameInstance>());
	Lessons->SetWriteToDisk(false);
	Lessons->LoadFromSaveGame(*LoadedFull);
	TestEqual(TEXT("The subsystem had progress before"), Lessons->GetProgress().Num(), 3);
	Lessons->LoadFromSaveGame(*Loaded);
	TestEqual(TEXT("A subsystem loading an old save has no progress"), Lessons->GetProgress().Num(), 0);
	TestEqual(TEXT("A subsystem loading an old save recommends A1"), Lessons->GetRecommendedNext(), FName(TEXT("A1")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolSubsystemOnGameInstance, "KiteSurf.School.SubsystemOnGameInstance", SchoolProgressTest::Flags)

bool FKiteSurfSchoolSubsystemOnGameInstance::RunTest(const FString& Parameters)
{
	using namespace SchoolProgressTest;
	TestFalse(TEXT("The subsystem is not made for a plain game instance"), GetDefault<ULessonSubsystem>()->ShouldCreateSubsystem(NewObject<UGameInstance>()));

	// The player's Settings file, if there is one, must come out of this untouched.
	const FString SettingsPath = SettingsSavePath();
	const bool bSettingsExisted = IFileManager::Get().FileExists(*SettingsPath);
	const FDateTime SettingsStamp = IFileManager::Get().GetTimeStamp(*SettingsPath);
	const int64 SettingsSize = IFileManager::Get().FileSize(*SettingsPath);
	const float VolumeBefore = FApp::GetVolumeMultiplier(); // Init applies the Settings slot's volume

	// A real game instance, initialised the way a standalone game does it: Init creates the
	// subsystems, then loads the Settings slot (a read).
	UKiteSurfGameInstance* GI = NewObject<UKiteSurfGameInstance>(GEngine);
	GI->InitializeStandalone();
	UWorld* World = GI->GetWorld();
	ULessonSubsystem* Lessons = GI->GetSubsystem<ULessonSubsystem>();
	if (TestNotNull(TEXT("Init creates the lesson subsystem on the KiteSurf game instance"), Lessons))
	{
		Lessons->SetWriteToDisk(false);
		Lessons->ResetProgress(); // forget whatever the Settings slot held

		// A trick book on the game instance, to show reset leaves it alone.
		UKiteSurfSaveGame* WithTricks = NewObject<UKiteSurfSaveGame>();
		WithTricks->TrickBook = MakeTrickBook();
		GI->ApplySaveGame(*WithTricks);
		TestEqual(TEXT("ApplySaveGame of a save with no progress leaves none"), Lessons->GetProgress().Num(), 0);
		TestEqual(TEXT("The game instance has the trick"), GI->GetTrickBook().Num(), 1);

		// Fresh progress.
		TestTrue(TEXT("A1 is unlocked at the start"), Lessons->IsUnlocked(TEXT("A1")));
		TestFalse(TEXT("B1 is locked at the start"), Lessons->IsUnlocked(TEXT("B1")));
		TestFalse(TEXT("An unknown lesson is not unlocked"), Lessons->IsUnlocked(TEXT("Z9")));
		TestEqual(TEXT("Continue goes to A1"), Lessons->GetRecommendedNext(), FName(TEXT("A1")));
		TestEqual(TEXT("No stars"), Lessons->GetTotalStars(), 0);

		TArray<FLessonListItem> List = Lessons->GetLessonList();
		TestEqual(TEXT("The lesson list has every catalogue lesson"), List.Num(), LessonCatalog::GetAll().Num());
		if (List.Num() == LessonCatalog::GetAll().Num())
		{
			TestEqual(TEXT("The list is in catalogue order"), List[0].LessonId, FName(TEXT("A1")));
			TestTrue(TEXT("A1: unlocked, available, new, no stars, titled, chapter A"), !List[0].bLocked && List[0].bAvailable && List[0].bNew
				&& List[0].Stars == 0 && List[0].Title.EqualTo(LessonCatalog::Find(TEXT("A1"))->Title) && List[0].Chapter == FName(TEXT("A")));
			TestTrue(TEXT("A2: locked, available, not new"), List[1].bLocked && List[1].bAvailable && !List[1].bNew);
		}

		// Results.
		TestFalse(TEXT("A failed A1 raises no stars"), Lessons->RecordLessonResult(TEXT("A1"), false, 0, 3.0f, false));
		TestFalse(TEXT("A1 is not new once attempted"), Lessons->GetLessonList()[0].bNew);
		TestTrue(TEXT("Passing A1 raises its stars"), Lessons->RecordLessonResult(TEXT("A1"), true, 2, 6.0f, false));
		AddExpectedMessagePlain(TEXT("no lesson Z9 in the catalogue"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
		TestFalse(TEXT("An unknown lesson's result is ignored"), Lessons->RecordLessonResult(TEXT("Z9"), true, 3, 1.0f, true));
		const FLessonRecord* A1 = Lessons->GetProgress().Find(TEXT("A1"));
		TestTrue(TEXT("A1: two attempts, one pass, two stars, a recent last played"), A1 && A1->Attempts == 2 && A1->Passes == 1 && A1->BestStars == 2
			&& (FDateTime::UtcNow() - A1->LastPlayedUtc).GetTotalMinutes() < 5.0);
		TestNull(TEXT("No record for the unknown lesson"), Lessons->GetProgress().Find(TEXT("Z9")));
		for (const TCHAR* Id : { TEXT("A2"), TEXT("A3"), TEXT("A4") })
		{
			Lessons->RecordLessonResult(Id, true, 1, NAN, false);
		}
		TestTrue(TEXT("B1 is unlocked after A4"), Lessons->IsUnlocked(TEXT("B1")));
		TestEqual(TEXT("Continue goes to A5"), Lessons->GetRecommendedNext(), FName(TEXT("A5")));
		TestEqual(TEXT("Total stars: 2 + 1 + 1 + 1"), Lessons->GetTotalStars(), 5);
		TestNearlyEqual(TEXT("Chapter A completion: 4 of 7"), Lessons->GetChapterCompletion(TEXT("A")), 4.0f / 7.0f, 1e-6f);
		TestEqual(TEXT("Chapter B completion: none"), Lessons->GetChapterCompletion(TEXT("B")), 0.0f);
		List = Lessons->GetLessonList();
		const FLessonListItem* A6 = List.FindByPredicate([](const FLessonListItem& I) { return I.LessonId == FName(TEXT("A6")); });
		const FLessonListItem* B1 = List.FindByPredicate([](const FLessonListItem& I) { return I.LessonId == FName(TEXT("B1")); });
		TestTrue(TEXT("A6: locked and unavailable"), A6 && A6->bLocked && !A6->bAvailable && !A6->bNew);
		TestTrue(TEXT("B1: unlocked and new"), B1 && !B1->bLocked && B1->bNew);

		// Starting (S3): locked and unknown lessons are refused; an unlocked one becomes pending. Travel
		// is off so this world (which has no rider) does not open the lesson's map.
		Lessons->SetTravelEnabled(false);
		TestFalse(TEXT("StartLesson: false for a locked lesson"), Lessons->StartLesson(TEXT("B2")));
		TestFalse(TEXT("StartLesson: false for an unknown lesson"), Lessons->StartLesson(TEXT("Z9")));
		TestEqual(TEXT("Nothing pending after refusals"), Lessons->GetPendingLessonId(), FName());
		TestTrue(TEXT("StartLesson: true for an unlocked lesson"), Lessons->StartLesson(TEXT("B1")));
		TestEqual(TEXT("The unlocked lesson is pending"), Lessons->GetPendingLessonId(), FName(TEXT("B1")));
		Lessons->ClearPendingLesson();

		// Round trip through the game instance's save path, minus the disk.
		const FLessonProgressBook Before = Lessons->GetProgress();
		UKiteSurfSaveGame* Written = NewObject<UKiteSurfSaveGame>();
		GI->WriteToSaveGame(*Written);
		TestTrue(TEXT("The game instance's WriteToSaveGame writes the subsystem's progress"), BooksEqual(Written->LessonProgress, Before));
		TestTrue(TEXT("...and its trick book"), TrickBooksEqual(Written->TrickBook, GI->GetTrickBook()));
		UKiteSurfSaveGame* Loaded = RoundTrip(Written);
		if (TestNotNull(TEXT("The game instance's save loads back"), Loaded))
		{
			Lessons->ResetProgress();
			TestEqual(TEXT("Reset empties the progress"), Lessons->GetProgress().Num(), 0);
			TestEqual(TEXT("Reset sends Continue back to A1"), Lessons->GetRecommendedNext(), FName(TEXT("A1")));
			TestEqual(TEXT("Reset leaves the trick book alone"), GI->GetTrickBook().Num(), 1);
			GI->ApplySaveGame(*Loaded);
			TestTrue(TEXT("The game instance's ApplySaveGame hands the progress back to the subsystem"), BooksEqual(Lessons->GetProgress(), Before));
		}

		// Reset, then save: no lessons, the same tricks.
		Lessons->ResetProgress();
		UKiteSurfSaveGame* AfterReset = NewObject<UKiteSurfSaveGame>();
		AfterReset->LessonProgress = Before;
		GI->WriteToSaveGame(*AfterReset);
		TestEqual(TEXT("After a reset the save has no lessons"), AfterReset->LessonProgress.Num(), 0);
		TestTrue(TEXT("After a reset the save keeps the trick book"), TrickBooksEqual(AfterReset->TrickBook, MakeTrickBook()));
	}

	GI->Shutdown();
	if (World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}
	FApp::SetVolumeMultiplier(VolumeBefore);

	TestTrue(TEXT("The Settings file still exists or still does not"), IFileManager::Get().FileExists(*SettingsPath) == bSettingsExisted);
	TestTrue(TEXT("The Settings file was not rewritten (time stamp)"), IFileManager::Get().GetTimeStamp(*SettingsPath) == SettingsStamp);
	TestEqual(TEXT("The Settings file was not rewritten (size)"), IFileManager::Get().FileSize(*SettingsPath), SettingsSize);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
