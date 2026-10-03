#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/TrickBook.h"
#include "UI/KiteSurfGameInstance.h"
#include "UI/KiteSurfSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests on the trick book (T2.7, docs/tricks/T2.md): the pure FTrickBook, and its round trip
// through UKiteSurfSaveGame and UKiteSurfGameInstance. Nothing here touches the player's own
// "Settings" slot: saves go through memory, or through the "TrickBookAutomationTest" slot, which
// is deleted afterwards.

namespace TrickBookTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	const FString TestSlot = TEXT("TrickBookAutomationTest");

	FJumpRecord MakeRecord(const FString& FamilyKey, const FString& Name, ELandingGrade Grade, float ScoreTotal, float ApexHeightCm,
		EJumpOutcome Outcome = EJumpOutcome::Landed)
	{
		FJumpRecord Record;
		Record.FamilyKey = FamilyKey;
		Record.TrickName = Name;
		Record.Grade = Grade;
		Record.Score.Total = ScoreTotal;
		Record.ApexHeightCm = ApexHeightCm;
		Record.Outcome = Outcome;
		return Record;
	}

	/** A fixed day in the past, so that landings recorded at FDateTime::UtcNow() sort after it. */
	FDateTime Day(int32 DayOfMonth, int32 Hour = 12)
	{
		return FDateTime(2025, 9, DayOfMonth, Hour, 0, 0);
	}

	bool EntriesEqual(const FTrickBookEntry& A, const FTrickBookEntry& B)
	{
		return A.FamilyKey == B.FamilyKey && A.BoardCategory == B.BoardCategory && A.DisplayName == B.DisplayName
			&& A.FirstLandedUtc == B.FirstLandedUtc && A.TimesLanded == B.TimesLanded && A.BestScore == B.BestScore
			&& A.BestHeightM == B.BestHeightM && A.BestGrade == B.BestGrade;
	}

	bool BooksEqual(const FTrickBook& A, const FTrickBook& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (!EntriesEqual(A.GetEntries()[Index], B.GetEntries()[Index]))
			{
				return false;
			}
		}
		return true;
	}

	/** A book with three entries: two tricks on a twin-tip, one of them also on a foil. */
	FTrickBook MakeBook()
	{
		FTrickBook Book;
		Book.RecordLanding(MakeRecord(TEXT("HH|L|-|I0|S-0|G"), TEXT("Back roll"), ELandingGrade::Clean, 41.5f, 812.0f), ETrickBoardCategory::TwinTip, Day(1));
		Book.RecordLanding(MakeRecord(TEXT("HH|L|-|I0|S-0|G"), TEXT("Back roll"), ELandingGrade::Stomped, 55.25f, 1033.0f), ETrickBoardCategory::TwinTip, Day(2));
		Book.RecordLanding(MakeRecord(TEXT("HH|LK|-|I|S-0|G"), TEXT("Kiteloop"), ELandingGrade::Sketchy, 120.0f, 1510.0f), ETrickBoardCategory::TwinTip, Day(3));
		Book.RecordLanding(MakeRecord(TEXT("HH|L|-|I0|S-0|G"), TEXT("Back roll"), ELandingGrade::Clean, 30.0f, 400.0f), ETrickBoardCategory::Foil, Day(4));
		return Book;
	}

	/** Gives a save non-default settings, so the round trips show they are kept next to the book. */
	void SetNonDefaultSettings(UKiteSurfSaveGame& Save)
	{
		Save.WindStrengthKnots = 27.0f;
		Save.MasterVolume = 0.4f;
		Save.MusicVolume = 0.2f;
		Save.RiderCharacterIndex = 1;
		Save.BoardSizeIndex = 2;
		Save.bHaptics = false;
		Save.bSpotSharks = false;
		Save.bOnboardingCompleted = true;
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickBookUnlocksOnFirstLanding, "KiteSurf.Trick.TrickBookUnlocksOnFirstLanding", TrickBookTest::Flags)

bool FKiteSurfTrickBookUnlocksOnFirstLanding::RunTest(const FString& Parameters)
{
	using namespace TrickBookTest;
	const FString BackRoll = TEXT("HH|L|-|I0|S-0|G");
	const FString FrontRoll = TEXT("HH|L|-|I1|S-0|G");

	FTrickBook Book;
	TestEqual(TEXT("A new book is empty"), Book.Num(), 0);
	TestNull(TEXT("Nothing is found in a new book"), Book.Find(BackRoll));

	// First landing: unlocks, with the bests from that landing.
	TestTrue(TEXT("The first landing of a trick is new"),
		Book.RecordLanding(MakeRecord(BackRoll, TEXT("Back roll"), ELandingGrade::Clean, 40.0f, 900.0f), ETrickBoardCategory::TwinTip, Day(1)));
	TestEqual(TEXT("One entry after the first landing"), Book.Num(), 1);
	const FTrickBookEntry* Entry = Book.Find(BackRoll);
	if (!TestNotNull(TEXT("The landed trick is found"), Entry))
	{
		return false;
	}
	TestEqual(TEXT("Display name from the record"), Entry->DisplayName, FString(TEXT("Back roll")));
	TestEqual(TEXT("Landed on a twin-tip by default"), Entry->BoardCategory, ETrickBoardCategory::TwinTip);
	TestEqual(TEXT("Landed once"), Entry->TimesLanded, 1);
	TestEqual(TEXT("Best score is the landing's"), Entry->BestScore, 40.0f);
	TestNearlyEqual(TEXT("Best height is the apex in metres (m)"), Entry->BestHeightM, 9.0f, 1e-4f);
	TestEqual(TEXT("Best grade is the landing's"), Entry->BestGrade, ELandingGrade::Clean);
	TestTrue(TEXT("First landed at the time given"), Entry->FirstLandedUtc == Day(1));

	// Repeat with a higher score, lower apex and better grade: not new; count and only the beaten bests move.
	TestFalse(TEXT("A repeat is not new"),
		Book.RecordLanding(MakeRecord(BackRoll, TEXT("Back roll"), ELandingGrade::Stomped, 52.0f, 700.0f), ETrickBoardCategory::TwinTip, Day(2)));
	Entry = Book.Find(BackRoll);
	TestEqual(TEXT("Still one entry after a repeat"), Book.Num(), 1);
	TestEqual(TEXT("Landed twice"), Entry->TimesLanded, 2);
	TestEqual(TEXT("A higher score raises the best score"), Entry->BestScore, 52.0f);
	TestNearlyEqual(TEXT("A lower apex keeps the best height (m)"), Entry->BestHeightM, 9.0f, 1e-4f);
	TestEqual(TEXT("A stomped landing raises the best grade"), Entry->BestGrade, ELandingGrade::Stomped);
	TestTrue(TEXT("A repeat keeps the first-landed time"), Entry->FirstLandedUtc == Day(1));

	// Repeat with a lower score, higher apex and worse grade.
	TestFalse(TEXT("A third landing is not new"),
		Book.RecordLanding(MakeRecord(BackRoll, TEXT("Back roll"), ELandingGrade::Sketchy, 10.0f, 1250.0f), ETrickBoardCategory::TwinTip, Day(3)));
	TestEqual(TEXT("Landed three times"), Entry->TimesLanded, 3);
	TestEqual(TEXT("A lower score keeps the best score"), Entry->BestScore, 52.0f);
	TestNearlyEqual(TEXT("A higher apex raises the best height (m)"), Entry->BestHeightM, 12.5f, 1e-4f);
	TestEqual(TEXT("A sketchy landing keeps the best grade"), Entry->BestGrade, ELandingGrade::Stomped);

	// Crashes unlock nothing and change nothing.
	TestFalse(TEXT("A crashed new trick is not new"),
		Book.RecordLanding(MakeRecord(FrontRoll, TEXT("Front roll"), ELandingGrade::Crash, 0.0f, 1500.0f, EJumpOutcome::Crashed), ETrickBoardCategory::TwinTip, Day(4)));
	TestNull(TEXT("A crashed trick is not in the book"), Book.Find(FrontRoll));
	TestFalse(TEXT("A Crash grade on a jump marked landed unlocks nothing either"),
		Book.RecordLanding(MakeRecord(FrontRoll, TEXT("Front roll"), ELandingGrade::Crash, 0.0f, 1500.0f), ETrickBoardCategory::TwinTip, Day(4)));
	TestFalse(TEXT("A crashed outcome with a landing grade unlocks nothing"),
		Book.RecordLanding(MakeRecord(FrontRoll, TEXT("Front roll"), ELandingGrade::Clean, 90.0f, 1500.0f, EJumpOutcome::Crashed), ETrickBoardCategory::TwinTip, Day(4)));
	TestNull(TEXT("Still no front roll after three crashes"), Book.Find(FrontRoll));
	TestFalse(TEXT("A crashed repeat is not new"),
		Book.RecordLanding(MakeRecord(BackRoll, TEXT("Back roll"), ELandingGrade::Crash, 999.0f, 5000.0f, EJumpOutcome::Crashed), ETrickBoardCategory::TwinTip, Day(4)));
	TestEqual(TEXT("A crashed repeat does not count as landed"), Entry->TimesLanded, 3);
	TestEqual(TEXT("A crashed repeat does not raise the best score"), Entry->BestScore, 52.0f);
	TestNearlyEqual(TEXT("A crashed repeat does not raise the best height (m)"), Entry->BestHeightM, 12.5f, 1e-4f);
	TestEqual(TEXT("Crashes leave one entry"), Book.Num(), 1);

	// A record the tracker has not named is ignored.
	TestFalse(TEXT("A record without a family key is not new"), Book.RecordLanding(MakeRecord(FString(), TEXT("?"), ELandingGrade::Clean, 5.0f, 300.0f)));
	TestEqual(TEXT("A record without a family key adds nothing"), Book.Num(), 1);

	// Sketchy counts as landed.
	TestTrue(TEXT("A sketchy first landing unlocks"),
		Book.RecordLanding(MakeRecord(FrontRoll, TEXT("Front roll"), ELandingGrade::Sketchy, 20.0f, 600.0f), ETrickBoardCategory::TwinTip, Day(5)));
	Entry = Book.Find(BackRoll);

	// Twin-tip and foil are kept apart (research G4).
	TestTrue(TEXT("The same trick on a foil is new"),
		Book.RecordLanding(MakeRecord(BackRoll, TEXT("Back roll"), ELandingGrade::Clean, 8.0f, 200.0f), ETrickBoardCategory::Foil, Day(6)));
	const FTrickBookEntry* FoilEntry = Book.Find(BackRoll, ETrickBoardCategory::Foil);
	if (TestNotNull(TEXT("The foil entry is found"), FoilEntry))
	{
		TestEqual(TEXT("The foil entry is on a foil"), FoilEntry->BoardCategory, ETrickBoardCategory::Foil);
		TestEqual(TEXT("The foil entry has its own count"), FoilEntry->TimesLanded, 1);
		TestEqual(TEXT("The foil entry has its own best score"), FoilEntry->BestScore, 8.0f);
	}
	Entry = Book.Find(BackRoll);
	TestEqual(TEXT("The twin-tip entry is untouched by the foil landing"), Entry->TimesLanded, 3);
	TestEqual(TEXT("The twin-tip best score is untouched by the foil landing"), Entry->BestScore, 52.0f);
	TestEqual(TEXT("Three entries: back roll twin-tip, front roll, back roll foil"), Book.Num(), 3);

	// Entries are in first-landed order.
	const TArray<FTrickBookEntry>& Entries = Book.GetEntries();
	TestTrue(TEXT("Entries are first landed first"), Entries.Num() == 3
		&& Entries[0].FamilyKey == BackRoll && Entries[0].BoardCategory == ETrickBoardCategory::TwinTip
		&& Entries[1].FamilyKey == FrontRoll
		&& Entries[2].FamilyKey == BackRoll && Entries[2].BoardCategory == ETrickBoardCategory::Foil);

	// Loading sorts by first landed and drops entries that could not have been written by RecordLanding.
	TArray<FTrickBookEntry> Loaded;
	Loaded.Add(Entries[2]);
	Loaded.Add(Entries[0]);
	Loaded.Add(Entries[1]);
	Loaded.Add(Entries[0]); // duplicate key
	FTrickBookEntry Unlanded = Entries[1];
	Unlanded.FamilyKey = TEXT("HH|L|-|I00|S-0|G");
	Unlanded.TimesLanded = 0;
	Loaded.Add(Unlanded);
	FTrickBookEntry Unkeyed = Entries[1];
	Unkeyed.FamilyKey.Reset();
	Loaded.Add(Unkeyed);
	FTrickBook Reloaded;
	Reloaded.SetEntries(Loaded);
	TestTrue(TEXT("SetEntries keeps the valid entries, in first-landed order"), BooksEqual(Reloaded, Book));

	// Through the game instance (no disk).
	UKiteSurfGameInstance* GI = NewObject<UKiteSurfGameInstance>();
	TestEqual(TEXT("A new game instance has an empty trick book"), GI->GetTrickBook().Num(), 0);
	TestTrue(TEXT("The game instance reports a first landing as new"), GI->RecordTrickLanding(MakeRecord(BackRoll, TEXT("Back roll"), ELandingGrade::Clean, 40.0f, 900.0f)));
	TestFalse(TEXT("The game instance reports a repeat as not new"), GI->RecordTrickLanding(MakeRecord(BackRoll, TEXT("Back roll"), ELandingGrade::Clean, 45.0f, 900.0f)));
	TestFalse(TEXT("The game instance ignores a crash"), GI->RecordTrickLanding(MakeRecord(FrontRoll, TEXT("Front roll"), ELandingGrade::Crash, 0.0f, 900.0f, EJumpOutcome::Crashed)));
	const FTrickBookEntry* GIEntry = GI->GetTrickBook().Find(BackRoll);
	TestTrue(TEXT("The game instance's book holds one twin-tip trick landed twice"), GI->GetTrickBook().Num() == 1 && GIEntry && GIEntry->TimesLanded == 2 && GIEntry->BestScore == 45.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickBookPersists, "KiteSurf.Trick.TrickBookPersists", TrickBookTest::Flags)

bool FKiteSurfTrickBookPersists::RunTest(const FString& Parameters)
{
	using namespace TrickBookTest;
	const FTrickBook Book = MakeBook();
	TestEqual(TEXT("The test book has three entries"), Book.Num(), 3);

	// In memory: the save game object as UGameplayStatics writes and reads it.
	UKiteSurfSaveGame* Save = NewObject<UKiteSurfSaveGame>();
	SetNonDefaultSettings(*Save);
	Save->TrickBook = Book;
	TArray<uint8> Bytes;
	TestTrue(TEXT("The save serialises to memory"), UGameplayStatics::SaveGameToMemory(Save, Bytes));
	UKiteSurfSaveGame* Loaded = Cast<UKiteSurfSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("The save loads back from memory"), Loaded))
	{
		return false;
	}
	TestTrue(TEXT("The trick book survives the round trip, entry for entry"), BooksEqual(Loaded->TrickBook, Book));
	TestEqual(TEXT("Wind is kept next to the book (kn)"), Loaded->WindStrengthKnots, 27.0f);
	TestEqual(TEXT("Master volume is kept next to the book"), Loaded->MasterVolume, 0.4f);
	TestEqual(TEXT("Rider is kept next to the book"), Loaded->RiderCharacterIndex, 1);
	TestFalse(TEXT("Haptics are kept next to the book"), Loaded->bHaptics);

	// Through the game instance: what SaveSettingsToDisk writes and LoadSettingsFromDisk reads, minus the disk.
	const float VolumeBefore = FApp::GetVolumeMultiplier(); // ApplySaveGame sets it from the save
	UKiteSurfGameInstance* Writer = NewObject<UKiteSurfGameInstance>();
	Writer->ApplySaveGame(*Loaded);
	TestTrue(TEXT("ApplySaveGame takes the book"), BooksEqual(Writer->GetTrickBook(), Book));
	TestTrue(TEXT("A landing after loading adds to the loaded book"), Writer->RecordTrickLanding(MakeRecord(TEXT("HH|L|-|I1|S-0|G"), TEXT("Front roll"), ELandingGrade::Clean, 33.0f, 640.0f)));
	UKiteSurfSaveGame* Written = NewObject<UKiteSurfSaveGame>();
	Writer->WriteToSaveGame(*Written);
	TestTrue(TEXT("WriteToSaveGame writes the book"), BooksEqual(Written->TrickBook, Writer->GetTrickBook()));
	TestEqual(TEXT("WriteToSaveGame writes the settings as before (kn)"), Written->WindStrengthKnots, 27.0f);
	TArray<uint8> GIBytes;
	UGameplayStatics::SaveGameToMemory(Written, GIBytes);
	UKiteSurfSaveGame* GILoaded = Cast<UKiteSurfSaveGame>(UGameplayStatics::LoadGameFromMemory(GIBytes));
	UKiteSurfGameInstance* Reader = NewObject<UKiteSurfGameInstance>();
	if (TestNotNull(TEXT("The game instance's save loads back"), GILoaded))
	{
		Reader->ApplySaveGame(*GILoaded);
	}
	TestEqual(TEXT("Four entries after the round trip through the game instance"), Reader->GetTrickBook().Num(), 4);
	TestTrue(TEXT("The book survives the round trip through the game instance"), BooksEqual(Reader->GetTrickBook(), Writer->GetTrickBook()));
	TestEqual(TEXT("The settings survive the round trip through the game instance (kn)"), Reader->PendingWindKnots, 27.0f);
	TestEqual(TEXT("The rider survives the round trip through the game instance"), Reader->RiderCharacter, Writer->RiderCharacter);
	FApp::SetVolumeMultiplier(VolumeBefore);

	// On disk, in a slot of the test's own: the default slot is the player's real save.
	TestNotEqual(TEXT("The test slot is not the player's slot"), TestSlot, UKiteSurfSaveGame::DefaultSaveSlot);
	UGameplayStatics::DeleteGameInSlot(TestSlot, UKiteSurfSaveGame::DefaultUserIndex);
	TestTrue(TEXT("The save is written to the test slot"), Save->SaveSettings(TestSlot));
	const UKiteSurfSaveGame* FromDisk = UKiteSurfSaveGame::LoadOrCreateSettings(TestSlot);
	TestTrue(TEXT("The trick book survives the round trip through the test slot"), FromDisk && BooksEqual(FromDisk->TrickBook, Book));
	TestTrue(TEXT("The settings survive the round trip through the test slot"), FromDisk && FromDisk->WindStrengthKnots == 27.0f && FromDisk->BoardSizeIndex == 2);
	UGameplayStatics::DeleteGameInSlot(TestSlot, UKiteSurfSaveGame::DefaultUserIndex);
	TestFalse(TEXT("The test slot is cleaned up"), UGameplayStatics::DoesSaveGameExist(TestSlot, UKiteSurfSaveGame::DefaultUserIndex));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickBookOldSaveLoads, "KiteSurf.Trick.TrickBookOldSaveLoads", TrickBookTest::Flags)

bool FKiteSurfTrickBookOldSaveLoads::RunTest(const FString& Parameters)
{
	using namespace TrickBookTest;
	const FName BookProperty = GET_MEMBER_NAME_CHECKED(UKiteSurfSaveGame, TrickBook);

	// A save with settings and a book, written as UGameplayStatics writes it: header, then the object.
	UKiteSurfSaveGame* Save = NewObject<UKiteSurfSaveGame>();
	SetNonDefaultSettings(*Save);
	Save->TrickBook = MakeBook();
	TArray<uint8> Full;
	UGameplayStatics::SaveGameToMemory(Save, Full);

	// Rebuild it as an older build wrote it: the same header, the object without the TrickBook property.
	const TArray<uint8> ObjectFull = SerializeObject(*Save);
	const TArray<uint8> ObjectOld = SerializeObject(*Save, BookProperty);
	const int32 HeaderBytes = Full.Num() - ObjectFull.Num();
	const bool bSplits = HeaderBytes > 0 && FMemory::Memcmp(Full.GetData() + HeaderBytes, ObjectFull.GetData(), ObjectFull.Num()) == 0;
	if (!TestTrue(TEXT("The save is a header followed by the object, as SaveGameToMemory writes it"), bSplits))
	{
		return false;
	}
	TestTrue(TEXT("Leaving the TrickBook property out makes the object smaller (the book is really gone)"), ObjectOld.Num() < ObjectFull.Num());
	TArray<uint8> Old(Full.GetData(), HeaderBytes);
	Old.Append(ObjectOld);

	UKiteSurfSaveGame* Loaded = Cast<UKiteSurfSaveGame>(UGameplayStatics::LoadGameFromMemory(Old));
	if (!TestNotNull(TEXT("A save without the trick book loads"), Loaded))
	{
		return false;
	}
	TestEqual(TEXT("A save without the trick book loads with an empty book"), Loaded->TrickBook.Num(), 0);
	TestEqual(TEXT("Its wind is kept (kn)"), Loaded->WindStrengthKnots, 27.0f);
	TestEqual(TEXT("Its master volume is kept"), Loaded->MasterVolume, 0.4f);
	TestEqual(TEXT("Its music volume is kept"), Loaded->MusicVolume, 0.2f);
	TestEqual(TEXT("Its rider is kept"), Loaded->RiderCharacterIndex, 1);
	TestEqual(TEXT("Its board is kept"), Loaded->BoardSizeIndex, 2);
	TestFalse(TEXT("Its haptics setting is kept"), Loaded->bHaptics);
	TestFalse(TEXT("Its sharks setting is kept"), Loaded->bSpotSharks);
	TestTrue(TEXT("Its onboarding flag is kept"), Loaded->bOnboardingCompleted);

	// The control: the same save with the property loads with the book.
	UKiteSurfSaveGame* LoadedFull = Cast<UKiteSurfSaveGame>(UGameplayStatics::LoadGameFromMemory(Full));
	TestTrue(TEXT("The same save with the property loads with its book"), LoadedFull && LoadedFull->TrickBook.Num() == 3);

	// The game instance takes the old save: an empty book, the settings as they were.
	const float VolumeBefore = FApp::GetVolumeMultiplier(); // ApplySaveGame sets it from the save
	UKiteSurfGameInstance* GI = NewObject<UKiteSurfGameInstance>();
	GI->RecordTrickLanding(MakeRecord(TEXT("HH|L|-|I0|S-0|G"), TEXT("Back roll"), ELandingGrade::Clean, 40.0f, 900.0f));
	GI->ApplySaveGame(*Loaded);
	TestEqual(TEXT("A game instance loading an old save has an empty book"), GI->GetTrickBook().Num(), 0);
	TestEqual(TEXT("A game instance loading an old save keeps its wind (kn)"), GI->PendingWindKnots, 27.0f);
	TestFalse(TEXT("A game instance loading an old save keeps its haptics setting"), GI->bHaptics);
	FApp::SetVolumeMultiplier(VolumeBefore);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
