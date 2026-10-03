#include "Tricks/TrickNaming.h"

// The helpers live in an anonymous namespace inside TrickNaming, so in a unity build they cannot
// meet helpers of the same name from other files, and the definitions below find them unqualified.
namespace TrickNaming
{
namespace
{
	// Parts are kept in lower case and the finished name gets one capital at the front, so a
	// part reads the same wherever it lands in a name. Words that keep a capital (S-loop, KGB)
	// are written that way.

	int32 HalfTurnsOf(int32 Degrees)
	{
		return FMath::RoundToInt(FMath::Abs(static_cast<float>(Degrees)) / 180.0f);
	}

	FString Capitalised(FString Text)
	{
		if (!Text.IsEmpty())
		{
			Text[0] = FChar::ToUpper(Text[0]);
		}
		return Text;
	}

	const TCHAR* SenseWord(ETrickSense Sense)
	{
		switch (Sense)
		{
		case ETrickSense::Frontside: return TEXT("frontside");
		case ETrickSense::Backside:  return TEXT("backside");
		default:                     return TEXT("");
		}
	}

	const TCHAR* InversionWord(ETrickInversion Inversion)
	{
		switch (Inversion)
		{
		case ETrickInversion::BackRoll:  return TEXT("back roll");
		case ETrickInversion::FrontRoll: return TEXT("front roll");
		case ETrickInversion::BackFlip:  return TEXT("backflip");
		case ETrickInversion::FrontFlip: return TEXT("front flip");
		default:                         return TEXT("");
		}
	}

	const TCHAR* BoardOffWord(ETrickBoardOff BoardOff)
	{
		switch (BoardOff)
		{
		case ETrickBoardOff::Plain:     return TEXT("board-off");
		case ETrickBoardOff::Superman:  return TEXT("superman");
		case ETrickBoardOff::TicTac:    return TEXT("tic tac");
		case ETrickBoardOff::BoardPass: return TEXT("board pass");
		case ETrickBoardOff::BoardFlip: return TEXT("board flip");
		default:                        return TEXT("");
		}
	}

	// docs/tricks.md 3.2, in zone order: nose, toe edge, heel edge, tail, behind heel, behind toe.
	const TCHAR* const GFrontHandGrabs[] = { TEXT("nose"), TEXT("mute"), TEXT("melon"), TEXT("seatbelt"), TEXT("chicken salad"), TEXT("thaipan") };
	const TCHAR* const GBackHandGrabs[] = { TEXT("crail"), TEXT("indy"), TEXT("stalefish"), TEXT("tail"), TEXT("roast beef"), TEXT("canadian bacon") };

	FString GrabWord(ETrickHand Hand, ETrickGrabZone Zone)
	{
		const int32 ZoneIndex = FMath::Clamp(static_cast<int32>(Zone), 0, static_cast<int32>(UE_ARRAY_COUNT(GFrontHandGrabs)) - 1);
		return Hand == ETrickHand::Front ? GFrontHandGrabs[ZoneIndex] : GBackHandGrabs[ZoneIndex];
	}

	/** "double ", "triple ", or a trailing " x4" from four up. */
	FString Counted(const FString& Word, int32 Count)
	{
		if (Count == 2)
		{
			return TEXT("double ") + Word;
		}
		if (Count == 3)
		{
			return TEXT("triple ") + Word;
		}
		if (Count >= 4)
		{
			return FString::Printf(TEXT("%s x%d"), *Word, Count);
		}
		return Word;
	}

	FString LoopPhrase(const FTrickLoop& Loop, int32 Count)
	{
		FString Word;
		switch (Loop.Kind)
		{
		case ETrickLoopKind::Megaloop: Word = Loop.bContra ? TEXT("contra megaloop") : TEXT("megaloop"); break;
		case ETrickLoopKind::HeliLoop: Word = TEXT("heli loop"); break;
		case ETrickLoopKind::SLoop:    Word = TEXT("S-loop"); break;
		case ETrickLoopKind::SnakeLoop: Word = TEXT("snake loop"); break;
		default:                       Word = Loop.bContra ? TEXT("contra loop") : TEXT("kiteloop"); break;
		}
		if (Loop.RollTiming == ELoopRollTiming::Early)
		{
			Word = TEXT("early ") + Word;
		}
		else if (Loop.RollTiming == ELoopRollTiming::Late)
		{
			Word = TEXT("late ") + Word;
		}
		return Counted(Word, Count);
	}

	bool SameLoop(const FTrickLoop& A, const FTrickLoop& B)
	{
		return A.Kind == B.Kind && A.bContra == B.bContra && A.RollTiming == B.RollTiming;
	}

	/** "backside 360", or "360" with no sense. */
	FString SpinPhrase(int32 HalfTurns, ETrickSense Sense)
	{
		const FString Degrees = FString::FromInt(HalfTurns * 180);
		return Sense == ETrickSense::None ? Degrees : FString::Printf(TEXT("%s %s"), SenseWord(Sense), *Degrees);
	}

	/** " to blind" or " to toeside"; empty for a heelside landing. */
	const TCHAR* LandingSuffix(ETrickStance Landing)
	{
		switch (Landing)
		{
		case ETrickStance::Blind:   return TEXT(" to blind");
		case ETrickStance::Toeside: return TEXT(" to toeside");
		default:                    return TEXT("");
		}
	}

	// ---------------------------------------------------------------------------------------
	// Freestyle table (docs/tricks.md 3.3 and 3.4). The first matching row wins, so specific rows
	// come before generic ones. T3.4 adds the GKA family and difficulty columns here.

	enum class ELandingRule : uint8 { Heelside, Toeside, Blind, Any };
	enum class EPassRule : uint8 { Any, NoPass, Required, AirOnly };
	enum class ENumberStyle : uint8 { None, Suffix, Concat };

	struct FFreestyleNameRow
	{
		ETrickStance Takeoff;
		ETrickMove Move;
		/** None: no rotation at all. */
		ETrickSense Sense;
		/** Rotation of the base trick in half turns; numbered rows also match more. */
		int32 BaseHalfTurns;
		/** Any: a landing that is not heelside adds "to blind" or "to toeside". */
		ELandingRule Landing;
		EPassRule Pass;
		/** The name at exactly the base rotation; nullptr for rows that are always numbered. */
		const TCHAR* Name;
		/** What a number goes after: "KGB" + " 5", "31" + "5". */
		const TCHAR* Stem;
		ENumberStyle Style;
	};

	using ETS = ETrickStance;
	using ETM = ETrickMove;
	using ESe = ETrickSense;

	const FFreestyleNameRow GFreestyleNames[] = {
		{ ETS::Heelside, ETM::BackRoll,  ESe::Backside,  2, ELandingRule::Heelside, EPassRule::Required, TEXT("KGB"),            TEXT("KGB"),  ENumberStyle::Suffix },
		{ ETS::Heelside, ETM::BackRoll,  ESe::Frontside, 2, ELandingRule::Heelside, EPassRule::Required, TEXT("Mobe"),           TEXT("Mobe"), ENumberStyle::Suffix },
		{ ETS::Heelside, ETM::BackRoll,  ESe::Backside,  1, ELandingRule::Blind,    EPassRule::Any,      TEXT("Back to blind"),  nullptr,      ENumberStyle::None },
		{ ETS::Heelside, ETM::FrontFlip, ESe::Frontside, 2, ELandingRule::Heelside, EPassRule::Required, TEXT("Slim chance"),    TEXT("Slim"), ENumberStyle::Suffix },
		{ ETS::Heelside, ETM::FrontFlip, ESe::Backside,  1, ELandingRule::Blind,    EPassRule::Any,      TEXT("Front blind"),    nullptr,      ENumberStyle::None },
		{ ETS::Heelside, ETM::Raley,     ESe::Frontside, 2, ELandingRule::Heelside, EPassRule::Required, TEXT("313"),            TEXT("31"),   ENumberStyle::Concat },
		{ ETS::Heelside, ETM::Raley,     ESe::Backside,  1, ELandingRule::Blind,    EPassRule::AirOnly,  TEXT("Blind judge"),    nullptr,      ENumberStyle::None },
		{ ETS::Heelside, ETM::Raley,     ESe::Backside,  1, ELandingRule::Blind,    EPassRule::Any,      TEXT("Raley to blind"), nullptr,      ENumberStyle::None },
		{ ETS::Heelside, ETM::Raley,     ESe::Frontside, 1, ELandingRule::Toeside,  EPassRule::Any,      TEXT("Krypt"),          nullptr,      ENumberStyle::None },
		{ ETS::Heelside, ETM::SBend,     ESe::Frontside, 2, ELandingRule::Heelside, EPassRule::Required, TEXT("S-mobe"),         nullptr,      ENumberStyle::None },
		{ ETS::Heelside, ETM::SBend,     ESe::Backside,  2, ELandingRule::Heelside, EPassRule::Required, TEXT("Heart attack"),   nullptr,      ENumberStyle::None },
		// The S-bend's own overhead turn is body spin: backside is the S-bend, frontside the hinterberger (GKA meaning).
		{ ETS::Heelside, ETM::SBend,     ESe::Backside,  2, ELandingRule::Heelside, EPassRule::NoPass,   TEXT("S-bend"),         nullptr,      ENumberStyle::None },
		{ ETS::Heelside, ETM::SBend,     ESe::Frontside, 2, ELandingRule::Heelside, EPassRule::NoPass,   TEXT("Hinterberger"),   nullptr,      ENumberStyle::None },
		{ ETS::Heelside, ETM::SBend,     ESe::None,      0, ELandingRule::Heelside, EPassRule::NoPass,   TEXT("S-bend"),         nullptr,      ENumberStyle::None },
		{ ETS::Toeside,  ETM::FrontRoll, ESe::Frontside, 2, ELandingRule::Heelside, EPassRule::Required, TEXT("Crow mobe"),      nullptr,      ENumberStyle::None },
		{ ETS::Toeside,  ETM::FrontRoll, ESe::Backside,  2, ELandingRule::Heelside, EPassRule::Required, TEXT("Dum dum"),        nullptr,      ENumberStyle::None },
		{ ETS::Heelside, ETM::BackFlip,  ESe::Backside,  2, ELandingRule::Heelside, EPassRule::Required, TEXT("Moby dick"),      nullptr,      ENumberStyle::None },
		{ ETS::Heelside, ETM::BackFlip,  ESe::None,      0, ELandingRule::Heelside, EPassRule::NoPass,   TEXT("Tantrum"),        nullptr,      ENumberStyle::None },
		{ ETS::Heelside, ETM::Raley,     ESe::None,      0, ELandingRule::Heelside, EPassRule::NoPass,   TEXT("Raley"),          nullptr,      ENumberStyle::None },
		{ ETS::Heelside, ETM::Pop,       ESe::Frontside, 1, ELandingRule::Any,      EPassRule::Required, nullptr,                TEXT("Frontside"), ENumberStyle::Suffix },
		{ ETS::Heelside, ETM::Pop,       ESe::Backside,  1, ELandingRule::Any,      EPassRule::Required, nullptr,                TEXT("Backside"),  ENumberStyle::Suffix },
		{ ETS::Heelside, ETM::Pop,       ESe::None,      0, ELandingRule::Heelside, EPassRule::NoPass,   TEXT("Unhooked pop"),   nullptr,      ENumberStyle::None },
	};

	/** The rotation the freestyle table matches: the passes summed, with the first pass's sense, or the body spin when there is no pass. */
	void FreestyleRotation(const FTrickSignature& S, int32& OutHalfTurns, ETrickSense& OutSense)
	{
		if (S.Passes.Num() > 0)
		{
			OutHalfTurns = 0;
			for (const FTrickPass& Pass : S.Passes)
			{
				OutHalfTurns += HalfTurnsOf(Pass.Degrees);
			}
			OutSense = S.Passes[0].Sense;
			return;
		}
		OutHalfTurns = FMath::Max(S.SpinHalfTurns, 0);
		OutSense = S.SpinSense;
	}

	bool LandingMatches(ELandingRule Rule, ETrickStance Landing)
	{
		switch (Rule)
		{
		case ELandingRule::Heelside: return Landing == ETrickStance::Heelside;
		case ELandingRule::Toeside:  return Landing == ETrickStance::Toeside;
		case ELandingRule::Blind:    return Landing == ETrickStance::Blind;
		default:                     return true;
		}
	}

	bool PassMatches(EPassRule Rule, const TArray<FTrickPass>& Passes)
	{
		switch (Rule)
		{
		case EPassRule::NoPass:   return Passes.Num() == 0;
		case EPassRule::Required: return Passes.Num() > 0;
		case EPassRule::AirOnly:
			if (Passes.Num() == 0)
			{
				return false;
			}
			for (const FTrickPass& Pass : Passes)
			{
				if (Pass.Kind != ETrickPassKind::Air)
				{
					return false;
				}
			}
			return true;
		default: return true;
		}
	}

	/** The table name, or false when no row fits. Rows only cover one take-off move with nothing else going on. */
	bool FreestyleTableName(const FTrickSignature& S, FString& OutName)
	{
		const int32 MoveCount = S.Inversions.Num() + ((S.bRaley || S.bSBend) ? 1 : 0);
		if (MoveCount > 1 || S.Loops.Num() > 0 || S.Grabs.Num() > 0 || S.bOneFooter
			|| S.BoardOff != ETrickBoardOff::None || S.bSwitchTakeoff)
		{
			return false;
		}

		const ETrickMove Move = FreestyleMove(S);
		int32 HalfTurns = 0;
		ETrickSense Sense = ETrickSense::None;
		FreestyleRotation(S, HalfTurns, Sense);

		for (const FFreestyleNameRow& Row : GFreestyleNames)
		{
			if (Row.Takeoff != S.TakeoffStance || Row.Move != Move)
			{
				continue;
			}
			if (Row.Sense == ETrickSense::None ? HalfTurns != 0 : (Sense != Row.Sense || HalfTurns < 1))
			{
				continue;
			}
			if (Row.Style == ENumberStyle::None ? HalfTurns != Row.BaseHalfTurns : HalfTurns < Row.BaseHalfTurns)
			{
				continue;
			}
			if (!LandingMatches(Row.Landing, S.LandingStance) || !PassMatches(Row.Pass, S.Passes))
			{
				continue;
			}

			const bool bNumbered = Row.Style != ENumberStyle::None && (HalfTurns > Row.BaseHalfTurns || Row.Name == nullptr);
			if (bNumbered)
			{
				const FString Number = PassNumber(HalfTurns * 180);
				OutName = Row.Style == ENumberStyle::Concat
					? FString(Row.Stem) + Number
					: FString::Printf(TEXT("%s %s"), Row.Stem, *Number);
			}
			else
			{
				OutName = Row.Name;
			}
			if (Row.Landing == ELandingRule::Any)
			{
				OutName += LandingSuffix(S.LandingStance);
			}
			return true;
		}
		return false;
	}

	// ---------------------------------------------------------------------------------------
	// Big air composition (docs/tricks.md 6.6).

	/** The composed name, or false when the composition cannot hold the signature (mixed loops or inversions, two grabs, a raley, too many parts). */
	bool ComposeBigAir(const FTrickSignature& S, FString& OutName)
	{
		if (S.bRaley || S.bSBend || S.Grabs.Num() > 1)
		{
			return false;
		}

		TArray<FString> Parts;
		if (S.bSwitchTakeoff)
		{
			Parts.Add(TEXT("switch"));
		}
		if (S.TakeoffStance == ETrickStance::Toeside)
		{
			Parts.Add(TEXT("toeside"));
		}
		else if (S.TakeoffStance == ETrickStance::Blind)
		{
			Parts.Add(TEXT("blind"));
		}

		if (S.Loops.Num() > 0)
		{
			for (const FTrickLoop& Loop : S.Loops)
			{
				if (!SameLoop(Loop, S.Loops[0]))
				{
					return false;
				}
			}
			Parts.Add(LoopPhrase(S.Loops[0], S.Loops.Num()));
		}

		if (S.Inversions.Num() > 0)
		{
			for (const ETrickInversion Inversion : S.Inversions)
			{
				if (Inversion != S.Inversions[0])
				{
					return false;
				}
			}
			Parts.Add(Counted(InversionWord(S.Inversions[0]), S.Inversions.Num()));
		}

		if (S.SpinHalfTurns > 0)
		{
			Parts.Add(SpinPhrase(S.SpinHalfTurns, S.SpinSense));
		}
		if (S.bOneFooter)
		{
			Parts.Add(TEXT("one-footer"));
		}
		if (S.BoardOff != ETrickBoardOff::None)
		{
			Parts.Add(BoardOffWord(S.BoardOff));
		}
		if (S.Grabs.Num() == 1)
		{
			Parts.Add(GrabWord(S.Grabs[0].Hand, S.Grabs[0].Zone));
		}

		if (Parts.Num() > MaxNamedParts)
		{
			return false;
		}

		OutName = Parts.Num() > 0 ? FString::Join(Parts, TEXT(" ")) : FString(TEXT("straight air"));
		OutName += LandingSuffix(S.LandingStance);
		OutName = Capitalised(OutName);
		return true;
	}

	const TCHAR* LoopKey(ETrickLoopKind Kind)
	{
		switch (Kind)
		{
		case ETrickLoopKind::Megaloop: return TEXT("M");
		case ETrickLoopKind::HeliLoop: return TEXT("H");
		case ETrickLoopKind::SLoop:    return TEXT("S");
		case ETrickLoopKind::SnakeLoop: return TEXT("N");
		default:                       return TEXT("K");
		}
	}

	const TCHAR* StanceKey(ETrickStance Stance)
	{
		switch (Stance)
		{
		case ETrickStance::Toeside: return TEXT("t");
		case ETrickStance::Blind:   return TEXT("b");
		default:                    return TEXT("h");
		}
	}

	const TCHAR* SenseKey(ETrickSense Sense)
	{
		switch (Sense)
		{
		case ETrickSense::Frontside: return TEXT("f");
		case ETrickSense::Backside:  return TEXT("b");
		default:                     return TEXT("n");
		}
	}
} // namespace
} // namespace TrickNaming

bool TrickNaming::IsFreestyle(const FTrickSignature& Signature)
{
	return !Signature.bHooked || Signature.Passes.Num() > 0;
}

ETrickMove TrickNaming::FreestyleMove(const FTrickSignature& Signature)
{
	if (Signature.bSBend)
	{
		return ETrickMove::SBend;
	}
	if (Signature.bRaley)
	{
		return ETrickMove::Raley;
	}
	if (Signature.Inversions.Num() > 0)
	{
		switch (Signature.Inversions[0])
		{
		case ETrickInversion::BackRoll:  return ETrickMove::BackRoll;
		case ETrickInversion::FrontRoll: return ETrickMove::FrontRoll;
		case ETrickInversion::FrontFlip: return ETrickMove::FrontFlip;
		case ETrickInversion::BackFlip:  return ETrickMove::BackFlip;
		default: break;
		}
	}
	return ETrickMove::Pop;
}

FString TrickNaming::PassNumber(int32 Degrees)
{
	const int32 HalfTurns = HalfTurnsOf(Degrees);
	if (HalfTurns <= 0)
	{
		return FString();
	}
	// Hundreds of degrees, rounded down: 180 -> 1, 360 -> 3, 540 -> 5, 900 -> 9, 1080 -> 10.
	return FString::FromInt(HalfTurns * 180 / 100);
}

FString TrickNaming::GrabName(ETrickHand Hand, ETrickGrabZone Zone)
{
	return Capitalised(GrabWord(Hand, Zone));
}

FString TrickNaming::Describe(const FTrickSignature& S)
{
	TArray<FString> Parts;
	if (S.bSwitchTakeoff)
	{
		Parts.Add(TEXT("switch"));
	}
	if (S.TakeoffStance == ETrickStance::Toeside)
	{
		Parts.Add(TEXT("toeside take-off"));
	}
	else if (S.TakeoffStance == ETrickStance::Blind)
	{
		Parts.Add(TEXT("blind take-off"));
	}

	// Runs of the same loop or inversion read as one part: "double kiteloop", "double back roll".
	for (int32 Start = 0; Start < S.Loops.Num();)
	{
		int32 End = Start + 1;
		while (End < S.Loops.Num() && SameLoop(S.Loops[End], S.Loops[Start]))
		{
			++End;
		}
		Parts.Add(LoopPhrase(S.Loops[Start], End - Start));
		Start = End;
	}

	if (S.bSBend)
	{
		Parts.Add(TEXT("S-bend"));
	}
	else if (S.bRaley)
	{
		Parts.Add(TEXT("raley"));
	}

	for (int32 Start = 0; Start < S.Inversions.Num();)
	{
		int32 End = Start + 1;
		while (End < S.Inversions.Num() && S.Inversions[End] == S.Inversions[Start])
		{
			++End;
		}
		Parts.Add(Counted(InversionWord(S.Inversions[Start]), End - Start));
		Start = End;
	}

	if (S.SpinHalfTurns > 0)
	{
		Parts.Add(SpinPhrase(S.SpinHalfTurns, S.SpinSense));
	}
	for (const FTrickPass& Pass : S.Passes)
	{
		const FString Spin = SpinPhrase(HalfTurnsOf(Pass.Degrees), Pass.Sense);
		Parts.Add(Spin + (Pass.Kind == ETrickPassKind::Surface ? TEXT(" surface pass") : TEXT(" pass")));
	}
	if (S.bOneFooter)
	{
		Parts.Add(TEXT("one-footer"));
	}
	if (S.BoardOff != ETrickBoardOff::None)
	{
		Parts.Add(BoardOffWord(S.BoardOff));
	}
	for (const FTrickGrab& Grab : S.Grabs)
	{
		Parts.Add(GrabWord(Grab.Hand, Grab.Zone));
	}

	if (Parts.Num() == 0)
	{
		return Capitalised(FString(S.bHooked ? TEXT("straight air") : TEXT("unhooked pop")) + LandingSuffix(S.LandingStance));
	}

	FString Result;
	if (Parts.Num() > MaxNamedParts)
	{
		Parts.SetNum(MaxNamedParts);
		Result = FString::Join(Parts, TEXT(" + ")) + TEXT(" + \u2026");
	}
	else
	{
		Result = FString::Join(Parts, TEXT(" + ")) + LandingSuffix(S.LandingStance);
	}
	return Capitalised(Result);
}

FString TrickNaming::Name(const FTrickSignature& Signature)
{
	FString Result;
	if (IsFreestyle(Signature))
	{
		if (FreestyleTableName(Signature, Result))
		{
			return Result;
		}
	}
	else if (ComposeBigAir(Signature, Result))
	{
		return Result;
	}
	return Describe(Signature);
}

FString TrickNaming::FamilyKey(const FTrickSignature& S)
{
	// One field per element, in a fixed order. Hold times, the board-off time and the grade are
	// left out; passes are rounded to half turns.
	FString Key = S.bHooked ? TEXT("H") : TEXT("U");
	Key += StanceKey(S.TakeoffStance);
	Key += S.bSwitchTakeoff ? TEXT("s") : TEXT("");

	Key += TEXT("|L");
	for (const FTrickLoop& Loop : S.Loops)
	{
		Key += LoopKey(Loop.Kind);
		Key += Loop.bContra ? TEXT("c") : TEXT("");
		Key += Loop.RollTiming == ELoopRollTiming::Early ? TEXT("e") : (Loop.RollTiming == ELoopRollTiming::Late ? TEXT("l") : TEXT(""));
	}

	Key += S.bSBend ? TEXT("|SB") : (S.bRaley ? TEXT("|R") : TEXT("|-"));

	Key += TEXT("|I");
	for (const ETrickInversion Inversion : S.Inversions)
	{
		Key += FString::FromInt(static_cast<int32>(Inversion));
	}

	Key += FString::Printf(TEXT("|S%s%d"), SenseKey(S.SpinSense), FMath::Max(S.SpinHalfTurns, 0));

	Key += TEXT("|G");
	for (const FTrickGrab& Grab : S.Grabs)
	{
		Key += FString::Printf(TEXT("%s%d"), Grab.Hand == ETrickHand::Front ? TEXT("f") : TEXT("b"), static_cast<int32>(Grab.Zone));
	}

	Key += FString::Printf(TEXT("|F%d|B%d"), S.bOneFooter ? 1 : 0, static_cast<int32>(S.BoardOff));

	Key += TEXT("|P");
	for (const FTrickPass& Pass : S.Passes)
	{
		Key += FString::Printf(TEXT("%s%d%s"), SenseKey(Pass.Sense), HalfTurnsOf(Pass.Degrees),
			Pass.Kind == ETrickPassKind::Surface ? TEXT("s") : TEXT("a"));
	}

	Key += TEXT("|>");
	Key += StanceKey(S.LandingStance);
	return Key;
}
