#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/TrickNaming.h"
#include "Tricks/TrickRecognition.h"
#include "Tricks/TrickScoring.h"
#include "Tricks/TrickSignature.h"

#if WITH_DEV_AUTOMATION_TESTS

// Named, not anonymous, so a unity build cannot merge these with another file's helpers.
namespace TrickNamingTest
{
	FTrickLoop Loop(ETrickLoopKind Kind, bool bContra = false, ELoopRollTiming Timing = ELoopRollTiming::None)
	{
		FTrickLoop Result;
		Result.Kind = Kind;
		Result.bContra = bContra;
		Result.RollTiming = Timing;
		return Result;
	}

	FTrickGrab Grab(ETrickHand Hand, ETrickGrabZone Zone, float HoldSeconds = 1.0f)
	{
		FTrickGrab Result;
		Result.Hand = Hand;
		Result.Zone = Zone;
		Result.HoldSeconds = HoldSeconds;
		return Result;
	}

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

	/** A jump with the landing facts every ScoreOrdering case shares: yaw 10 deg, 3 g, kite at 60 deg. */
	FJumpRecord Record(float ApexHeightCm)
	{
		FJumpRecord Result;
		Result.Index = 0;
		Result.ApexHeightCm = ApexHeightCm;
		Result.AirtimeSeconds = 6.0f;
		Result.LandingYawDeg = 10.0f;
		Result.LandingG = 3.0f;
		Result.KiteElevationAtLandingDeg = 60.0f;
		return Result;
	}

	/** A completed loop that started with the rider HeightAtStartCm up, at the apex. */
	FJumpLoop CompletedLoop(float MinElevationDeg, float HeightAtStartCm, float PeakTensionN)
	{
		FJumpLoop Result;
		Result.Loop.Index = 0;
		Result.Loop.Direction = 1;
		Result.Loop.RiderTravelSide = 1;
		Result.Loop.TurnDeg = 360.0f;
		Result.Loop.bCompleted = true;
		Result.Loop.DurationSeconds = 2.0f;
		Result.Loop.StartElevationDeg = 70.0f;
		Result.Loop.MinElevationDeg = MinElevationDeg;
		Result.Loop.PeakTensionN = PeakTensionN;
		Result.RiderHeightAtStartCm = HeightAtStartCm;
		Result.StartSinceApexSeconds = 0.0f;
		Result.StartSinceTakeoffSeconds = 3.0f;
		return Result;
	}

	FTrickScore Score(const FJumpRecord& InRecord)
	{
		return TrickScoring::ScoreJump(InRecord, TrickRecognition::SignatureFromJump(InRecord));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickNamesBigAir, "KiteSurf.Trick.NamesBigAir",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickNamesBigAir::RunTest(const FString& Parameters)
{
	using namespace TrickNamingTest;

	{
		FTrickSignature S;
		S.Loops.Add(Loop(ETrickLoopKind::Megaloop));
		S.Inversions.Add(ETrickInversion::BackRoll);
		TestEqual(TEXT("Megaloop + back roll"), TrickNaming::Name(S), FString(TEXT("Megaloop back roll")));
	}
	{
		FTrickSignature S;
		S.Loops.Add(Loop(ETrickLoopKind::Kiteloop));
		S.Loops.Add(Loop(ETrickLoopKind::Kiteloop));
		TestEqual(TEXT("Two kiteloops"), TrickNaming::Name(S), FString(TEXT("Double kiteloop")));
		S.Loops.Add(Loop(ETrickLoopKind::Kiteloop));
		TestEqual(TEXT("Three kiteloops"), TrickNaming::Name(S), FString(TEXT("Triple kiteloop")));
		S.Loops.Add(Loop(ETrickLoopKind::Kiteloop));
		TestEqual(TEXT("Four kiteloops"), TrickNaming::Name(S), FString(TEXT("Kiteloop x4")));
	}
	{
		FTrickSignature S;
		S.Loops.Add(Loop(ETrickLoopKind::Kiteloop, true));
		S.BoardOff = ETrickBoardOff::Plain;
		TestEqual(TEXT("Contra kiteloop + board-off"), TrickNaming::Name(S), FString(TEXT("Contra loop board-off")));
	}
	{
		FTrickSignature S;
		S.Inversions = { ETrickInversion::BackRoll, ETrickInversion::BackRoll };
		TestEqual(TEXT("Two back rolls"), TrickNaming::Name(S), FString(TEXT("Double back roll")));
	}
	{
		FTrickSignature S;
		S.Grabs.Add(Grab(ETrickHand::Back, ETrickGrabZone::ToeEdge));
		TestEqual(TEXT("Back hand on the toe edge"), TrickNaming::Name(S), FString(TEXT("Indy")));
	}
	{
		const FTrickSignature S;
		TestEqual(TEXT("Nothing at all"), TrickNaming::Name(S), FString(TEXT("Straight air")));
	}
	{
		FTrickSignature S;
		S.Loops.Add(Loop(ETrickLoopKind::Megaloop, false, ELoopRollTiming::Late));
		S.Inversions.Add(ETrickInversion::BackRoll);
		TestEqual(TEXT("Late roll in a megaloop"), TrickNaming::Name(S), FString(TEXT("Late megaloop back roll")));
	}
	{
		FTrickSignature S;
		S.SpinHalfTurns = 2;
		S.SpinSense = ETrickSense::Backside;
		S.Grabs.Add(Grab(ETrickHand::Front, ETrickGrabZone::HeelEdge));
		TestEqual(TEXT("Hooked spin with a grab"), TrickNaming::Name(S), FString(TEXT("Backside 360 melon")));
		S.SpinHalfTurns = 1;
		S.LandingStance = ETrickStance::Blind;
		S.Grabs.Reset();
		TestEqual(TEXT("Hooked half turn to blind"), TrickNaming::Name(S), FString(TEXT("Backside 180 to blind")));
	}
	{
		// Mixed inversions do not compose; they are described part by part.
		FTrickSignature S;
		S.Inversions = { ETrickInversion::BackRoll, ETrickInversion::FrontRoll };
		TestEqual(TEXT("Back roll then front roll"), TrickNaming::Name(S), FString(TEXT("Back roll + front roll")));
	}
	{
		// Nine parts: the description stops at MaxNamedParts and ends in an ellipsis.
		FTrickSignature S;
		S.Loops.Add(Loop(ETrickLoopKind::Megaloop));
		S.Inversions = { ETrickInversion::BackRoll, ETrickInversion::FrontRoll, ETrickInversion::BackFlip };
		S.SpinHalfTurns = 2;
		S.SpinSense = ETrickSense::Backside;
		S.bOneFooter = true;
		S.BoardOff = ETrickBoardOff::Plain;
		S.Grabs.Add(Grab(ETrickHand::Back, ETrickGrabZone::ToeEdge));
		S.Grabs.Add(Grab(ETrickHand::Front, ETrickGrabZone::HeelEdge));
		const FString Name = TrickNaming::Name(S);
		TestTrue(FString::Printf(TEXT("Nine parts end in an ellipsis (%s)"), *Name), Name.EndsWith(TEXT("\u2026")));
		TestTrue(FString::Printf(TEXT("Nine parts start with the loop (%s)"), *Name), Name.StartsWith(TEXT("Megaloop + back roll + front roll")));
		TArray<FString> Parts;
		Name.ParseIntoArray(Parts, TEXT(" + "));
		TestEqual(TEXT("MaxNamedParts parts and the ellipsis"), Parts.Num(), TrickNaming::MaxNamedParts + 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickNamesFreestyle, "KiteSurf.Trick.NamesFreestyle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickNamesFreestyle::RunTest(const FString& Parameters)
{
	using namespace TrickNamingTest;

	{
		FTrickSignature S = Unhooked();
		S.Inversions.Add(ETrickInversion::BackRoll);
		S.Passes.Add(Pass(ETrickSense::Backside, 540));
		TestEqual(TEXT("Back roll + backside 540 pass"), TrickNaming::Name(S), FString(TEXT("KGB 5")));
		S.Passes[0].Degrees = 360;
		TestEqual(TEXT("Back roll + backside 360 pass"), TrickNaming::Name(S), FString(TEXT("KGB")));
	}
	{
		FTrickSignature S = Unhooked();
		S.bRaley = true;
		S.Passes.Add(Pass(ETrickSense::Frontside, 360));
		TestEqual(TEXT("Raley + frontside 360 pass"), TrickNaming::Name(S), FString(TEXT("313")));
		S.Passes[0].Degrees = 540;
		TestEqual(TEXT("Raley + frontside 540 pass"), TrickNaming::Name(S), FString(TEXT("315")));
		S.Passes[0].Degrees = 1080;
		TestEqual(TEXT("Raley + frontside 1080 pass"), TrickNaming::Name(S), FString(TEXT("3110")));
	}
	{
		FTrickSignature S = Unhooked();
		S.Inversions.Add(ETrickInversion::BackRoll);
		S.SpinHalfTurns = 1;
		S.SpinSense = ETrickSense::Backside;
		S.LandingStance = ETrickStance::Blind;
		TestEqual(TEXT("Back roll + backside 180, landing blind, no pass"), TrickNaming::Name(S), FString(TEXT("Back to blind")));
	}
	{
		FTrickSignature S = Unhooked();
		S.Inversions.Add(ETrickInversion::FrontFlip);
		S.Passes.Add(Pass(ETrickSense::Frontside, 360));
		S.Passes.Add(Pass(ETrickSense::Frontside, 360));
		TestEqual(TEXT("Front flip + two frontside 360 passes"), TrickNaming::Name(S), FString(TEXT("Slim 7")));
	}
	{
		FTrickSignature S = Unhooked();
		S.Passes.Add(Pass(ETrickSense::Frontside, 360));
		TestEqual(TEXT("Pop + frontside 360 pass"), TrickNaming::Name(S), FString(TEXT("Frontside 3")));
	}
	{
		// A pass names the jump as freestyle even hooked in.
		FTrickSignature S;
		S.Passes.Add(Pass(ETrickSense::Backside, 540));
		TestEqual(TEXT("Hooked pop + backside 540 pass"), TrickNaming::Name(S), FString(TEXT("Backside 5")));
	}
	{
		FTrickSignature S = Unhooked();
		S.Inversions.Add(ETrickInversion::BackRoll);
		S.Passes.Add(Pass(ETrickSense::Backside, 540));
		S.LandingStance = ETrickStance::Blind;
		TestEqual(TEXT("No row fits: described"), TrickNaming::Name(S), FString(TEXT("Back roll + backside 540 pass to blind")));
	}
	{
		FTrickSignature S = Unhooked();
		TestEqual(TEXT("Unhooked, nothing else"), TrickNaming::Name(S), FString(TEXT("Unhooked pop")));
		S.bRaley = true;
		TestEqual(TEXT("Raley alone"), TrickNaming::Name(S), FString(TEXT("Raley")));
		S.Passes.Add(Pass(ETrickSense::Backside, 180, ETrickPassKind::Air));
		S.LandingStance = ETrickStance::Blind;
		TestEqual(TEXT("Raley + backside 180 air pass to blind"), TrickNaming::Name(S), FString(TEXT("Blind judge")));
		S.Passes[0].Kind = ETrickPassKind::Surface;
		TestEqual(TEXT("Raley + backside 180 surface pass to blind"), TrickNaming::Name(S), FString(TEXT("Raley to blind")));
	}
	{
		FTrickSignature S = Unhooked();
		S.Inversions.Add(ETrickInversion::BackFlip);
		TestEqual(TEXT("Unhooked backflip"), TrickNaming::Name(S), FString(TEXT("Tantrum")));
	}
	{
		// Without a pass a back roll with a backside 360 is not a KGB.
		FTrickSignature S = Unhooked();
		S.Inversions.Add(ETrickInversion::BackRoll);
		S.SpinHalfTurns = 2;
		S.SpinSense = ETrickSense::Backside;
		TestEqual(TEXT("Back roll + backside 360 spin, no pass"), TrickNaming::Name(S), FString(TEXT("Back roll + backside 360")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickFamilyKey, "KiteSurf.Trick.FamilyKeyIgnoresHoldAndGrade",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickFamilyKey::RunTest(const FString& Parameters)
{
	using namespace TrickNamingTest;

	FTrickSignature A;
	A.Loops.Add(Loop(ETrickLoopKind::Megaloop));
	A.Inversions.Add(ETrickInversion::BackRoll);
	A.Grabs.Add(Grab(ETrickHand::Back, ETrickGrabZone::ToeEdge, 0.8f));
	A.BoardOff = ETrickBoardOff::Plain;
	A.BoardOffSeconds = 0.6f;
	A.Grade = ELandingGrade::Clean;

	FTrickSignature B = A;
	B.Grabs[0].HoldSeconds = 2.4f;
	B.BoardOffSeconds = 1.5f;
	B.Grade = ELandingGrade::Sketchy;

	const FString KeyA = TrickNaming::FamilyKey(A);
	TestFalse(TEXT("The key is not empty"), KeyA.IsEmpty());
	TestEqual(TEXT("Grab hold time, board-off time and grade do not change the key"), TrickNaming::FamilyKey(B), KeyA);
	TestEqual(TEXT("...nor the name"), TrickNaming::Name(B), TrickNaming::Name(A));

	FTrickSignature MoreLoops = A;
	MoreLoops.Loops.Add(Loop(ETrickLoopKind::Megaloop));
	TestNotEqual(TEXT("One more loop is another family"), TrickNaming::FamilyKey(MoreLoops), KeyA);

	FTrickSignature OtherGrab = A;
	OtherGrab.Grabs[0].Zone = ETrickGrabZone::HeelEdge;
	TestNotEqual(TEXT("Another grab is another family"), TrickNaming::FamilyKey(OtherGrab), KeyA);

	FTrickSignature Contra = A;
	Contra.Loops[0].bContra = true;
	TestNotEqual(TEXT("A contra loop is another family"), TrickNaming::FamilyKey(Contra), KeyA);

	FTrickSignature ToBlind = A;
	ToBlind.LandingStance = ETrickStance::Blind;
	TestNotEqual(TEXT("Landing blind is another family"), TrickNaming::FamilyKey(ToBlind), KeyA);

	// Pass rotations are rounded to half turns.
	FTrickSignature Kgb = Unhooked();
	Kgb.Inversions.Add(ETrickInversion::BackRoll);
	Kgb.Passes.Add(Pass(ETrickSense::Backside, 540));
	FTrickSignature KgbShort = Kgb;
	KgbShort.Passes[0].Degrees = 520;
	TestEqual(TEXT("A 520 pass counts as a 540"), TrickNaming::FamilyKey(KgbShort), TrickNaming::FamilyKey(Kgb));
	FTrickSignature KgbHooked = Kgb;
	KgbHooked.bHooked = true;
	TestNotEqual(TEXT("Hooked and unhooked are different families"), TrickNaming::FamilyKey(KgbHooked), TrickNaming::FamilyKey(Kgb));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickGrabAndPassNames, "KiteSurf.Trick.GrabAndPassNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickGrabAndPassNames::RunTest(const FString& Parameters)
{
	// docs/tricks.md 3.2.
	const TCHAR* const Front[] = { TEXT("Nose"), TEXT("Mute"), TEXT("Melon"), TEXT("Seatbelt"), TEXT("Chicken salad"), TEXT("Thaipan") };
	const TCHAR* const Back[] = { TEXT("Crail"), TEXT("Indy"), TEXT("Stalefish"), TEXT("Tail"), TEXT("Roast beef"), TEXT("Canadian bacon") };
	for (int32 Zone = 0; Zone < 6; ++Zone)
	{
		TestEqual(FString::Printf(TEXT("Front hand, zone %d"), Zone), TrickNaming::GrabName(ETrickHand::Front, static_cast<ETrickGrabZone>(Zone)), FString(Front[Zone]));
		TestEqual(FString::Printf(TEXT("Back hand, zone %d"), Zone), TrickNaming::GrabName(ETrickHand::Back, static_cast<ETrickGrabZone>(Zone)), FString(Back[Zone]));
	}

	TestEqual(TEXT("180"), TrickNaming::PassNumber(180), FString(TEXT("1")));
	TestEqual(TEXT("360"), TrickNaming::PassNumber(360), FString(TEXT("3")));
	TestEqual(TEXT("540"), TrickNaming::PassNumber(540), FString(TEXT("5")));
	TestEqual(TEXT("720"), TrickNaming::PassNumber(720), FString(TEXT("7")));
	TestEqual(TEXT("900"), TrickNaming::PassNumber(900), FString(TEXT("9")));
	TestEqual(TEXT("1080"), TrickNaming::PassNumber(1080), FString(TEXT("10")));
	TestEqual(TEXT("350 rounds to 360"), TrickNaming::PassNumber(350), FString(TEXT("3")));
	TestEqual(TEXT("0 has no number"), TrickNaming::PassNumber(0), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickScoreOrdering, "KiteSurf.Trick.ScoreOrdering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickScoreOrdering::RunTest(const FString& Parameters)
{
	using namespace TrickNamingTest;

	// Three body weights for an 85 kg rider is about 2500 N; 3000 N pulls a megaloop.
	const float MegaPullN = 3000.0f;

	// A 20 m loop (kite down to 30 deg, thrown at the apex) beats a 20 m straight jump.
	const FJumpRecord Straight20 = Record(2000.0f);
	FJumpRecord Loop20 = Record(2000.0f);
	Loop20.Loops.Add(CompletedLoop(30.0f, 2000.0f, 2000.0f));
	const FTrickScore StraightScore = Score(Straight20);
	const FTrickScore LoopScore = Score(Loop20);
	TestEqual(TEXT("Yaw 10 deg, 3 g, kite 60 deg is stomped"), TrickRecognition::SignatureFromJump(Straight20).Grade, ELandingGrade::Stomped);
	TestTrue(TEXT("A straight jump scores something"), StraightScore.Total > 0.0f);
	TestTrue(FString::Printf(TEXT("20 m loop (%.2f) beats 20 m straight (%.2f)"), LoopScore.Total, StraightScore.Total), LoopScore.Total > StraightScore.Total);

	// A low megaloop (kite to 10 deg) beats a high-kite loop (kite to 50 deg) at the same height and timing.
	FJumpRecord Mega20 = Record(2000.0f);
	Mega20.Loops.Add(CompletedLoop(10.0f, 2000.0f, MegaPullN));
	FJumpRecord High20 = Record(2000.0f);
	High20.Loops.Add(CompletedLoop(50.0f, 2000.0f, MegaPullN));
	const FTrickScore MegaScore20 = Score(Mega20);
	const FTrickScore HighScore20 = Score(High20);
	const FTrickSignature MegaSignature = TrickRecognition::SignatureFromJump(Mega20);
	TestEqual(TEXT("Kite to 10 deg from 20 m with 3000 N is one loop"), MegaSignature.Loops.Num(), 1);
	if (MegaSignature.Loops.Num() == 1)
	{
		TestEqual(TEXT("...and a megaloop"), MegaSignature.Loops[0].Kind, ETrickLoopKind::Megaloop);
	}
	TestEqual(TEXT("Kite to 50 deg is a plain kiteloop"), TrickRecognition::SignatureFromJump(High20).Loops[0].Kind, ETrickLoopKind::Kiteloop);
	TestTrue(FString::Printf(TEXT("Low megaloop (%.2f) beats high-kite loop (%.2f)"), MegaScore20.Total, HighScore20.Total), MegaScore20.Total > HighScore20.Total);

	// The same loops thrown from 10 m score under the 20 m ones (research F4).
	FJumpRecord Mega10 = Record(1000.0f);
	Mega10.Loops.Add(CompletedLoop(10.0f, 1000.0f, MegaPullN));
	FJumpRecord High10 = Record(1000.0f);
	High10.Loops.Add(CompletedLoop(50.0f, 1000.0f, MegaPullN));
	const FTrickScore MegaScore10 = Score(Mega10);
	const FTrickScore HighScore10 = Score(High10);
	TestTrue(FString::Printf(TEXT("Megaloop at 20 m (%.2f) beats it at 10 m (%.2f)"), MegaScore20.Total, MegaScore10.Total), MegaScore20.Total > MegaScore10.Total);
	TestTrue(FString::Printf(TEXT("High-kite loop at 20 m (%.2f) beats it at 10 m (%.2f)"), HighScore20.Total, HighScore10.Total), HighScore20.Total > HighScore10.Total);

	// A loop thrown late (at the apex) beats the same loop thrown on the way up from 5 m.
	FJumpRecord Early20 = Record(2000.0f);
	Early20.Loops.Add(CompletedLoop(10.0f, 500.0f, MegaPullN));
	TestTrue(TEXT("A loop at the apex beats one thrown low"), MegaScore20.Total > Score(Early20).Total);

	// A crash scores nothing, exactly.
	FJumpRecord Crashed = Mega20;
	Crashed.Outcome = EJumpOutcome::Crashed;
	const FTrickSignature CrashedSignature = TrickRecognition::SignatureFromJump(Crashed);
	TestEqual(TEXT("A crashed record grades Crash"), CrashedSignature.Grade, ELandingGrade::Crash);
	TestTrue(TEXT("A crash totals exactly 0"), TrickScoring::ScoreJump(Crashed, CrashedSignature).Total == 0.0f);
	// Even with a signature that claims a good landing, a crashed record pays nothing.
	TestTrue(TEXT("A crashed record totals 0 whatever the signature says"), TrickScoring::ScoreJump(Crashed, MegaSignature).Total == 0.0f);

	// A cleaner landing scores more than a sketchy one.
	FTrickSignature Sketchy = MegaSignature;
	Sketchy.Grade = ELandingGrade::Sketchy;
	TestTrue(TEXT("Stomped beats sketchy"), MegaScore20.Total > TrickScoring::ScoreJump(Mega20, Sketchy).Total);

	// Technicality: a back roll scores more than the same jump without one.
	FTrickSignature Rolled = MegaSignature;
	Rolled.Inversions.Add(ETrickInversion::BackRoll);
	TestTrue(TEXT("A megaloop back roll beats a megaloop"), TrickScoring::ScoreJump(Mega20, Rolled).Total > MegaScore20.Total);

	// Repeats: 1, 0.75, 0.5, 0.25, then 0.1 of the raw score.
	{
		FTrickSession Session;
		const FString Key = TrickNaming::FamilyKey(MegaSignature);
		const float Raw = 40.0f;
		const float First = Session.Add(Key, Raw);
		const float Second = Session.Add(Key, Raw);
		TestNearlyEqual(TEXT("The first landing pays in full"), First, Raw, 1e-4f);
		TestNearlyEqual(TEXT("The second identical family pays 0.75 of the first"), Second, 0.75f * First, 1e-4f);
		TestTrue(TEXT("A repeat scores less"), Second < First);
		Session.Add(Key, Raw);
		Session.Add(Key, Raw);
		const float Fifth = Session.Add(Key, Raw);
		const float Sixth = Session.Add(Key, Raw);
		TestNearlyEqual(TEXT("The fifth pays 0.10"), Fifth, 0.10f * First, 1e-4f);
		TestNearlyEqual(TEXT("The sixth pays 0.10"), Sixth, 0.10f * First, 1e-4f);
		TestEqual(TEXT("Six landings counted"), Session.GetCount(Key), 6);
		const float Other = Session.Add(TrickNaming::FamilyKey(Rolled), Raw);
		TestNearlyEqual(TEXT("A different family pays in full"), Other, Raw, 1e-4f);
		TestNearlyEqual(TEXT("RepeatFactor(0)"), TrickScoring::RepeatFactor(0), 1.0f, 1e-6f);
		TestNearlyEqual(TEXT("RepeatFactor(1)"), TrickScoring::RepeatFactor(1), 0.75f, 1e-6f);
		TestNearlyEqual(TEXT("RepeatFactor(10)"), TrickScoring::RepeatFactor(10), 0.10f, 1e-6f);
	}

	// GradeLanding at each boundary (the limits are inclusive).
	TestEqual(TEXT("Crashed"), TrickScoring::GradeLanding(0.0f, 1.0f, 80.0f, true), ELandingGrade::Crash);
	TestEqual(TEXT("Yaw 20, 4 g, kite 45: stomped"), TrickScoring::GradeLanding(20.0f, 4.0f, 45.0f, false), ELandingGrade::Stomped);
	TestEqual(TEXT("Yaw -20 counts as 20"), TrickScoring::GradeLanding(-20.0f, 4.0f, 45.0f, false), ELandingGrade::Stomped);
	TestEqual(TEXT("Yaw 20.1: clean"), TrickScoring::GradeLanding(20.1f, 4.0f, 45.0f, false), ELandingGrade::Clean);
	TestEqual(TEXT("4.1 g: clean"), TrickScoring::GradeLanding(10.0f, 4.1f, 60.0f, false), ELandingGrade::Clean);
	TestEqual(TEXT("8 g: still clean"), TrickScoring::GradeLanding(10.0f, 8.0f, 60.0f, false), ELandingGrade::Clean);
	TestEqual(TEXT("8.1 g: sketchy"), TrickScoring::GradeLanding(10.0f, 8.1f, 60.0f, false), ELandingGrade::Sketchy);
	TestEqual(TEXT("Kite at 44.9: sketchy (hot)"), TrickScoring::GradeLanding(10.0f, 3.0f, 44.9f, false), ELandingGrade::Sketchy);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickSignatureFromJump, "KiteSurf.Trick.SignatureFromJump",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickSignatureFromJump::RunTest(const FString& Parameters)
{
	using namespace TrickNamingTest;

	FJumpRecord R = Record(2000.0f);
	R.Loops.Add(CompletedLoop(10.0f, 2000.0f, 3000.0f));
	FJumpLoop Contra = CompletedLoop(40.0f, 1500.0f, 1500.0f);
	Contra.Loop.Direction = -1;
	R.Loops.Add(Contra);
	FJumpLoop Partial = CompletedLoop(40.0f, 1500.0f, 1500.0f);
	Partial.Loop.bCompleted = false;
	Partial.Loop.TurnDeg = 200.0f;
	R.Loops.Add(Partial);
	FJumpLoop Heli = CompletedLoop(60.0f, 1200.0f, 900.0f);
	Heli.StartSinceApexSeconds = 1.5f;
	R.Loops.Add(Heli);

	const FTrickSignature S = TrickRecognition::SignatureFromJump(R);
	TestTrue(TEXT("Hooked"), S.bHooked);
	TestEqual(TEXT("Heelside take-off"), S.TakeoffStance, ETrickStance::Heelside);
	TestEqual(TEXT("No rotation"), S.Inversions.Num() + S.SpinHalfTurns, 0);
	TestEqual(TEXT("Completed loops only"), S.Loops.Num(), 3);
	if (S.Loops.Num() == 3)
	{
		TestEqual(TEXT("Low, high and hard: megaloop"), S.Loops[0].Kind, ETrickLoopKind::Megaloop);
		TestFalse(TEXT("Turning with the travel is not contra"), S.Loops[0].bContra);
		TestEqual(TEXT("Kite to 40 deg: kiteloop"), S.Loops[1].Kind, ETrickLoopKind::Kiteloop);
		TestTrue(TEXT("Turning against the travel is contra"), S.Loops[1].bContra);
		TestEqual(TEXT("After the apex with the kite high: heli loop"), S.Loops[2].Kind, ETrickLoopKind::HeliLoop);
	}
	TestEqual(TEXT("Stomped landing"), S.Grade, ELandingGrade::Stomped);

	// Each megaloop condition on its own is needed.
	const FLoopClassifySettings Settings;
	TestEqual(TEXT("Started at 7 m: kiteloop"), TrickRecognition::ClassifyLoop(CompletedLoop(10.0f, 700.0f, 3000.0f), Settings), ETrickLoopKind::Kiteloop);
	TestEqual(TEXT("Kite to 25 deg: kiteloop"), TrickRecognition::ClassifyLoop(CompletedLoop(25.0f, 2000.0f, 3000.0f), Settings), ETrickLoopKind::Kiteloop);
	TestEqual(TEXT("2000 N (2.4 body weights): kiteloop"), TrickRecognition::ClassifyLoop(CompletedLoop(10.0f, 2000.0f, 2000.0f), Settings), ETrickLoopKind::Kiteloop);

	// Hot landing: the kite low at touchdown grades sketchy.
	FJumpRecord Hot = R;
	Hot.KiteElevationAtLandingDeg = 30.0f;
	TestEqual(TEXT("Kite at 30 deg at landing: sketchy"), TrickRecognition::SignatureFromJump(Hot).Grade, ELandingGrade::Sketchy);

	const FJumpRecord& Constant = R;
	TestEqual(TEXT("CountCompletedLoops"), Constant.CountCompletedLoops(), 3);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
