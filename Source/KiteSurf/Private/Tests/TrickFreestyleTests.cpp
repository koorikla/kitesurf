#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Tricks/FreestyleScoring.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/LandingEvaluator.h"
#include "Tricks/TrickNaming.h"
#include "Tricks/TrickScoring.h"
#include "Tricks/TrickSignature.h"

#if WITH_DEV_AUTOMATION_TESTS

// Named, not anonymous, so a unity build cannot merge these with another file's helpers.
namespace TrickFreestyleTest
{
	FTrickPass Pass(ETrickSense Sense, int32 Degrees, ETrickPassKind Kind = ETrickPassKind::Air)
	{
		FTrickPass Result;
		Result.Sense = Sense;
		Result.Degrees = Degrees;
		Result.Kind = Kind;
		return Result;
	}

	FTrickSignature Unhooked()
	{
		FTrickSignature Result;
		Result.bHooked = false;
		return Result;
	}

	FTrickSignature Move(ETrickInversion Inversion)
	{
		FTrickSignature Result = Unhooked();
		Result.Inversions.Add(Inversion);
		return Result;
	}

	FTrickSignature Raley()
	{
		FTrickSignature Result = Unhooked();
		Result.bRaley = true;
		return Result;
	}

	FTrickSignature SBend()
	{
		FTrickSignature Result = Unhooked();
		Result.bSBend = true;
		return Result;
	}

	FString FamilyName(EGkaFamily Family)
	{
		return StaticEnum<EGkaFamily>()->GetNameStringByValue(static_cast<int64>(Family));
	}

	/** Checks the name, the family, the difficulty and FreestyleFamily's return. */
	void Expect(FAutomationTestBase& Test, const FTrickSignature& S, const TCHAR* Name, EGkaFamily Family, float Difficulty)
	{
		const FString Got = TrickNaming::Name(S);
		if (Name)
		{
			Test.TestEqual(FString::Printf(TEXT("%s: name"), Name), Got, FString(Name));
		}
		EGkaFamily GotFamily = EGkaFamily::RaleyBased;
		float GotDifficulty = -1.0f;
		const bool bFamily = TrickNaming::FreestyleFamily(S, &GotFamily, &GotDifficulty);
		Test.TestEqual(FString::Printf(TEXT("%s: family"), *Got), *FamilyName(GotFamily), *FamilyName(Family));
		Test.TestNearlyEqual(FString::Printf(TEXT("%s: difficulty"), *Got), GotDifficulty, Difficulty, 1e-6f);
		Test.TestEqual(FString::Printf(TEXT("%s: FreestyleFamily returns whether there is a family"), *Got), bFamily, Family != EGkaFamily::None);
	}

	FScoredTrick Scored(EGkaFamily Family, float Score, const TCHAR* Name)
	{
		FScoredTrick Result;
		Result.Family = Family;
		Result.Score = Score;
		Result.Name = Name;
		return Result;
	}

	FLandingVerdict Verdict(ELandingGrade Grade)
	{
		FLandingVerdict Result;
		Result.Grade = Grade;
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickFreestyleNameTable, "KiteSurf.Trick.FreestyleNameTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickFreestyleNameTable::RunTest(const FString& Parameters)
{
	using namespace TrickFreestyleTest;
	using EF = EGkaFamily;
	using ES = ETrickSense;

	// Every row of the table, with its family and difficulty (docs/tricks/T3.md section 4).
	{
		FTrickSignature S = Move(ETrickInversion::BackRoll);
		S.Passes.Add(Pass(ES::Backside, 360));
		Expect(*this, S, TEXT("KGB"), EF::KgbSlim, 4.0f);
		S.Passes[0].Degrees = 540;
		Expect(*this, S, TEXT("KGB 5"), EF::KgbSlim, 4.5f);
	}
	{
		FTrickSignature S = Move(ETrickInversion::BackRoll);
		S.Passes.Add(Pass(ES::Frontside, 360));
		Expect(*this, S, TEXT("Mobe"), EF::Mobes, 4.0f);
		S.Passes[0].Degrees = 540;
		Expect(*this, S, TEXT("Mobe 5"), EF::Mobes, 4.0f);
	}
	{
		FTrickSignature S = Move(ETrickInversion::BackRoll);
		S.SpinHalfTurns = 1;
		S.SpinSense = ES::Backside;
		S.LandingStance = ETrickStance::Blind;
		Expect(*this, S, TEXT("Back to blind"), EF::KgbSlim, 3.0f);
	}
	{
		FTrickSignature S = Move(ETrickInversion::FrontFlip);
		S.Passes.Add(Pass(ES::Frontside, 360));
		Expect(*this, S, TEXT("Slim chance"), EF::KgbSlim, 4.5f);
		S.Passes.Add(Pass(ES::Frontside, 360));
		Expect(*this, S, TEXT("Slim 7"), EF::KgbSlim, 4.5f);
	}
	{
		FTrickSignature S = Move(ETrickInversion::FrontFlip);
		S.SpinHalfTurns = 1;
		S.SpinSense = ES::Backside;
		S.LandingStance = ETrickStance::Blind;
		Expect(*this, S, TEXT("Front blind"), EF::KgbSlim, 4.0f);
	}
	{
		FTrickSignature S = Raley();
		S.Passes.Add(Pass(ES::Frontside, 360));
		Expect(*this, S, TEXT("313"), EF::RaleyBased, 3.5f);
		S.Passes[0].Degrees = 540;
		Expect(*this, S, TEXT("315"), EF::RaleyBased, 4.0f);
		S.Passes[0].Degrees = 1080;
		Expect(*this, S, TEXT("3110"), EF::RaleyBased, 4.0f);
	}
	{
		FTrickSignature S = Raley();
		S.Passes.Add(Pass(ES::Backside, 180, ETrickPassKind::Air));
		S.LandingStance = ETrickStance::Blind;
		Expect(*this, S, TEXT("Blind judge"), EF::RaleyBased, 3.0f);
		S.Passes[0].Kind = ETrickPassKind::Surface;
		Expect(*this, S, TEXT("Raley to blind"), EF::RaleyBased, 2.5f);
	}
	{
		FTrickSignature S = Raley();
		S.SpinHalfTurns = 1;
		S.SpinSense = ES::Frontside;
		S.LandingStance = ETrickStance::Toeside;
		Expect(*this, S, TEXT("Krypt"), EF::RaleyBased, 2.5f);
	}
	{
		FTrickSignature S = SBend();
		S.Passes.Add(Pass(ES::Frontside, 360));
		Expect(*this, S, TEXT("S-mobe"), EF::HinterHeart, 4.5f);
		S.Passes[0].Sense = ES::Backside;
		Expect(*this, S, TEXT("Heart attack"), EF::HinterHeart, 4.5f);
	}
	{
		FTrickSignature S = SBend();
		S.SpinHalfTurns = 2;
		S.SpinSense = ES::Backside;
		Expect(*this, S, TEXT("S-bend"), EF::HinterHeart, 3.0f);
		S.SpinSense = ES::Frontside;
		Expect(*this, S, TEXT("Hinterberger"), EF::HinterHeart, 3.5f);
		S.SpinHalfTurns = 0;
		S.SpinSense = ES::None;
		Expect(*this, S, TEXT("S-bend"), EF::HinterHeart, 3.0f);
	}
	{
		FTrickSignature S = Move(ETrickInversion::FrontRoll);
		S.TakeoffStance = ETrickStance::Toeside;
		S.Passes.Add(Pass(ES::Frontside, 360));
		Expect(*this, S, TEXT("Crow mobe"), EF::ToesideBlind, 4.0f);
		S.Passes[0].Sense = ES::Backside;
		Expect(*this, S, TEXT("Dum dum"), EF::ToesideBlind, 4.0f);
	}
	{
		FTrickSignature S = Move(ETrickInversion::BackFlip);
		Expect(*this, S, TEXT("Tantrum"), EF::KgbSlim, 3.0f);
		S.Passes.Add(Pass(ES::Backside, 360));
		Expect(*this, S, TEXT("Moby dick"), EF::Mobes, 4.5f);
	}
	Expect(*this, Raley(), TEXT("Raley"), EF::RaleyBased, 2.0f);
	{
		FTrickSignature S = Unhooked();
		S.Passes.Add(Pass(ES::Frontside, 360));
		Expect(*this, S, TEXT("Frontside 3"), EF::Combos, 3.0f);
		S.Passes[0].Degrees = 180;
		S.LandingStance = ETrickStance::Toeside;
		Expect(*this, S, TEXT("Frontside 1 to toeside"), EF::Combos, 3.0f);
	}
	{
		FTrickSignature S = Unhooked();
		S.Passes.Add(Pass(ES::Backside, 540));
		S.LandingStance = ETrickStance::Blind;
		Expect(*this, S, TEXT("Backside 5 to blind"), EF::Combos, 3.0f);
	}
	// The unhooked pop is named but is not a family trick.
	Expect(*this, Unhooked(), TEXT("Unhooked pop"), EF::None, 1.0f);

	// Families no row names.
	{
		// A kite loop with a pass.
		FTrickSignature S = Unhooked();
		S.Loops.AddDefaulted();
		S.Passes.Add(Pass(ES::Backside, 360));
		Expect(*this, S, nullptr, EF::KiteLoopPasses, 5.0f);
	}
	{
		// Two inversions with a pass.
		FTrickSignature S = Move(ETrickInversion::BackRoll);
		S.Inversions.Add(ETrickInversion::BackRoll);
		S.Passes.Add(Pass(ES::Frontside, 180));
		Expect(*this, S, nullptr, EF::InvertedDoubles, 5.0f);
	}
	{
		// A blind take-off: no row (the KGB row is for heelside take-offs), still ToesideBlind.
		FTrickSignature S = Move(ETrickInversion::BackRoll);
		S.TakeoffStance = ETrickStance::Blind;
		S.Passes.Add(Pass(ES::Backside, 360));
		Expect(*this, S, nullptr, EF::ToesideBlind, 5.0f);
	}
	{
		// A hooked big air jump is not freestyle.
		FTrickSignature S;
		S.Loops.AddDefaulted();
		S.Inversions.Add(ETrickInversion::BackRoll);
		Expect(*this, S, TEXT("Kiteloop back roll"), EF::None, 0.0f);
	}
	{
		// No row fits and no family rule applies: described, no family.
		FTrickSignature S = Move(ETrickInversion::BackRoll);
		S.Passes.Add(Pass(ES::Backside, 540));
		S.LandingStance = ETrickStance::Blind;
		Expect(*this, S, TEXT("Back roll + backside 540 pass to blind"), EF::None, 0.0f);
	}

	// Null out pointers are fine.
	TestTrue(TEXT("FreestyleFamily with null outs"), TrickNaming::FreestyleFamily(Raley(), nullptr, nullptr));

	// Groups.
	const EGkaFamily HeelFamilies[] = { EF::RaleyBased, EF::KgbSlim, EF::HinterHeart, EF::Mobes };
	const EGkaFamily VarietyFamilies[] = { EF::Rewinds, EF::ToesideBlind, EF::Combos, EF::InvertedDoubles, EF::KiteLoopPasses };
	for (const EGkaFamily Family : HeelFamilies)
	{
		TestEqual(FamilyName(Family) + TEXT(" is in the heelside group"), FreestyleScoring::GroupOf(Family), EGkaGroup::Heelside);
	}
	for (const EGkaFamily Family : VarietyFamilies)
	{
		TestEqual(FamilyName(Family) + TEXT(" is in the variety group"), FreestyleScoring::GroupOf(Family), EGkaGroup::Variety);
	}
	TestEqual(TEXT("None is in no group"), FreestyleScoring::GroupOf(EF::None), EGkaGroup::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickFreestyleHeatScore, "KiteSurf.Trick.FreestyleHeatScore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickFreestyleHeatScore::RunTest(const FString& Parameters)
{
	using namespace TrickFreestyleTest;
	using EF = EGkaFamily;

	// The worked example in docs/tricks/T3.md T3.6.
	{
		const TArray<FScoredTrick> Heat = {
			Scored(EF::RaleyBased, 6.0f, TEXT("Raley")),     // 0
			Scored(EF::RaleyBased, 7.5f, TEXT("313")),       // 1: same family, better
			Scored(EF::KgbSlim, 8.0f, TEXT("KGB")),          // 2
			Scored(EF::Mobes, 7.0f, TEXT("Mobe")),           // 3: out on the heelside limit
			Scored(EF::ToesideBlind, 5.0f, TEXT("Toeside")), // 4
			Scored(EF::Rewinds, 6.5f, TEXT("313 to blind")), // 5
			Scored(EF::Combos, 0.0f, TEXT("Crash")),         // 6
		};
		const FHeatResult R = FreestyleScoring::ScoreFreestyleHeat(Heat);
		TestNearlyEqual(TEXT("8.0 + 7.5 + 6.5 + 5.0 + 7 = 34.0"), R.Total, 34.0f, 1e-4f);
		TestNearlyEqual(TEXT("Tricks 27.0"), R.TrickTotal, 27.0f, 1e-4f);
		TestNearlyEqual(TEXT("Variety bonus for four families: 7"), R.VarietyBonus, 7.0f, 1e-6f);
		TestEqual(TEXT("Two heelside tricks count"), R.HeelsideCounted, 2);
		TestEqual(TEXT("Two variety tricks count"), R.VarietyCounted, 2);
		const TArray<int32> Expected = { 2, 1, 5, 4 };
		TestTrue(TEXT("Counting: KGB, 313, rewind, toeside, best first"), R.CountingIdx == Expected);
		TestFalse(TEXT("The raley is beaten by the 313 in its family"), R.CountingIdx.Contains(0));
		TestFalse(TEXT("The mobe is excluded by the heelside limit"), R.CountingIdx.Contains(3));
		TestFalse(TEXT("The crash does not count"), R.CountingIdx.Contains(6));
	}

	// Variety limit: five strong variety families and a weak heelside trick give 3 + 1.
	{
		const TArray<FScoredTrick> Heat = {
			Scored(EF::Rewinds, 9.0f, TEXT("A")),
			Scored(EF::ToesideBlind, 8.0f, TEXT("B")),
			Scored(EF::Combos, 7.0f, TEXT("C")),
			Scored(EF::InvertedDoubles, 6.0f, TEXT("D")),
			Scored(EF::KiteLoopPasses, 5.0f, TEXT("E")),
			Scored(EF::RaleyBased, 1.0f, TEXT("F")),
		};
		const FHeatResult R = FreestyleScoring::ScoreFreestyleHeat(Heat);
		TestEqual(TEXT("At most three variety tricks"), R.VarietyCounted, 3);
		TestEqual(TEXT("...so the weak heelside trick fills the fourth slot"), R.HeelsideCounted, 1);
		TestNearlyEqual(TEXT("9 + 8 + 7 + 1 + 7"), R.Total, 32.0f, 1e-4f);
	}
	{
		// Only variety tricks: three count, bonus for three families.
		const TArray<FScoredTrick> Heat = {
			Scored(EF::Rewinds, 9.0f, TEXT("A")),
			Scored(EF::ToesideBlind, 8.0f, TEXT("B")),
			Scored(EF::Combos, 7.0f, TEXT("C")),
			Scored(EF::InvertedDoubles, 6.0f, TEXT("D")),
		};
		const FHeatResult R = FreestyleScoring::ScoreFreestyleHeat(Heat);
		TestEqual(TEXT("Variety only: three count"), R.CountingIdx.Num(), 3);
		TestNearlyEqual(TEXT("9 + 8 + 7 + bonus 4"), R.Total, 28.0f, 1e-4f);
	}
	{
		// Heelside only: two count. The repeat in a family counts once, whatever the order.
		const TArray<FScoredTrick> Heat = {
			Scored(EF::KgbSlim, 8.0f, TEXT("KGB")),
			Scored(EF::KgbSlim, 9.0f, TEXT("KGB 5")),
			Scored(EF::Mobes, 7.0f, TEXT("Mobe")),
			Scored(EF::RaleyBased, 6.0f, TEXT("313")),
		};
		const FHeatResult R = FreestyleScoring::ScoreFreestyleHeat(Heat);
		TestEqual(TEXT("Heelside only: two count"), R.CountingIdx.Num(), 2);
		TestNearlyEqual(TEXT("9 + 7 + bonus 2"), R.Total, 18.0f, 1e-4f);
		TestTrue(TEXT("The better KGB counts"), R.CountingIdx.Contains(1) && !R.CountingIdx.Contains(0));
	}
	{
		// One trick: bonus 1. Nothing: 0. Family None never counts.
		TestNearlyEqual(TEXT("One trick: 6 + bonus 1"), FreestyleScoring::ScoreFreestyleHeat(TArray<FScoredTrick>{ Scored(EF::Mobes, 6.0f, TEXT("Mobe")) }).Total, 7.0f, 1e-4f);
		TestNearlyEqual(TEXT("No tricks: 0"), FreestyleScoring::ScoreFreestyleHeat(TArray<FScoredTrick>()).Total, 0.0f, 1e-6f);
		TestNearlyEqual(TEXT("Family None: 0"), FreestyleScoring::ScoreFreestyleHeat(TArray<FScoredTrick>{ Scored(EF::None, 5.0f, TEXT("Unhooked pop")) }).Total, 0.0f, 1e-6f);
	}
	{
		// Only the first seven attempts count.
		TArray<FScoredTrick> Heat;
		for (int32 I = 0; I < 7; ++I)
		{
			Heat.Add(Scored(EF::Mobes, 5.0f, TEXT("Mobe")));
		}
		Heat.Add(Scored(EF::KgbSlim, 9.0f, TEXT("Eighth")));
		const FHeatResult R = FreestyleScoring::ScoreFreestyleHeat(Heat);
		TestNearlyEqual(TEXT("The eighth trick is past the seven attempts"), R.Total, 6.0f, 1e-4f);
		FFreestyleHeatRules AllCount;
		AllCount.Attempts = 0;
		TestNearlyEqual(TEXT("With no attempt limit it counts"), FreestyleScoring::ScoreFreestyleHeat(Heat, AllCount).Total, 16.0f, 1e-4f);
	}

	// One trick's score: clamp(2 x difficulty x execution x clamp(0.8 + 0.1 x apex, 0.8, 1.2), 0.1, 10).
	{
		FTrickSignature Kgb = Move(ETrickInversion::BackRoll);
		Kgb.Passes.Add(Pass(ETrickSense::Backside, 360));
		TestNearlyEqual(TEXT("KGB stomped from 3 m: 2 x 4 x 1 x 1.1"),
			FreestyleScoring::FreestyleTrickScore(Kgb, Verdict(ELandingGrade::Stomped), 3.0f), 8.8f, 1e-4f);
		TestNearlyEqual(TEXT("KGB clean from 3 m: 2 x 4 x 0.85 x 1.1"),
			FreestyleScoring::FreestyleTrickScore(Kgb, Verdict(ELandingGrade::Clean), 3.0f), 7.48f, 1e-4f);
		TestNearlyEqual(TEXT("KGB stomped from 0 m: height factor 0.8"),
			FreestyleScoring::FreestyleTrickScore(Kgb, Verdict(ELandingGrade::Stomped), 0.0f), 6.4f, 1e-4f);
		TestNearlyEqual(TEXT("KGB stomped from 10 m: height factor capped at 1.2"),
			FreestyleScoring::FreestyleTrickScore(Kgb, Verdict(ELandingGrade::Stomped), 10.0f), 9.6f, 1e-4f);
		TestTrue(TEXT("A crashed KGB scores exactly 0"),
			FreestyleScoring::FreestyleTrickScore(Kgb, Verdict(ELandingGrade::Crash), 3.0f) == 0.0f);

		FTrickSignature SMobe = SBend();
		SMobe.Passes.Add(Pass(ETrickSense::Frontside, 360));
		TestNearlyEqual(TEXT("S-mobe stomped from 4 m: capped at 10"),
			FreestyleScoring::FreestyleTrickScore(SMobe, Verdict(ELandingGrade::Stomped), 4.0f), 10.0f, 1e-4f);
		TestNearlyEqual(TEXT("Unhooked pop, sketchy, 0 m: 2 x 1 x 0.5 x 0.8"),
			FreestyleScoring::FreestyleTrickScore(Unhooked(), Verdict(ELandingGrade::Sketchy), 0.0f), 0.8f, 1e-4f);
		TestNearlyEqual(TEXT("No difficulty (hooked jump): floor 0.1"),
			FreestyleScoring::FreestyleTrickScore(FTrickSignature(), Verdict(ELandingGrade::Stomped), 3.0f), 0.1f, 1e-6f);

		const FScoredTrick Trick = FreestyleScoring::MakeScoredTrick(Kgb, Verdict(ELandingGrade::Stomped), 3.0f);
		TestEqual(TEXT("MakeScoredTrick: family"), *FamilyName(Trick.Family), *FamilyName(EF::KgbSlim));
		TestNearlyEqual(TEXT("MakeScoredTrick: score"), Trick.Score, 8.8f, 1e-4f);
		TestEqual(TEXT("MakeScoredTrick: name"), Trick.Name, FString(TEXT("KGB")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickSnakeLoopExtremity, "KiteSurf.Trick.SnakeLoopExtremity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickSnakeLoopExtremity::RunTest(const FString& Parameters)
{
	// The contra or S-loop extremity bonus also goes to a snake loop, once per jump.
	const FTrickScoringSettings Settings;
	FJumpRecord Record;
	Record.ApexHeightCm = 1500.0f;

	const auto ExtremityWith = [&](TArray<ETrickLoopKind> Kinds)
	{
		FTrickSignature S;
		for (const ETrickLoopKind Kind : Kinds)
		{
			FTrickLoop Loop;
			Loop.Kind = Kind;
			S.Loops.Add(Loop);
		}
		return TrickScoring::ScoreJump(Record, S, Settings).Extremity;
	};

	TestNearlyEqual(TEXT("A plain kiteloop gets no bonus"), ExtremityWith({ ETrickLoopKind::Kiteloop }), 0.0f, 1e-6f);
	TestNearlyEqual(TEXT("An S-loop gets the bonus"), ExtremityWith({ ETrickLoopKind::SLoop }), Settings.ContraOrSLoopBonus, 1e-6f);
	TestNearlyEqual(TEXT("A snake loop gets the bonus"), ExtremityWith({ ETrickLoopKind::SnakeLoop }), Settings.ContraOrSLoopBonus, 1e-6f);
	TestNearlyEqual(TEXT("A snake loop and an S-loop get it once"), ExtremityWith({ ETrickLoopKind::SnakeLoop, ETrickLoopKind::SLoop }), Settings.ContraOrSLoopBonus, 1e-6f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
