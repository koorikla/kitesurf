#include "Tricks/TrickRecognition.h"
#include "KiteSurfUnits.h"

ETrickLoopKind TrickRecognition::ClassifyLoop(const FJumpLoop& Loop, const FLoopClassifySettings& Settings)
{
	const FKiteLoopRecord& Kite = Loop.Loop;
	if (Loop.StartSinceApexSeconds > 0.0f
		&& Kite.MinElevationDeg >= Settings.HeliLoopMinElevationDeg
		&& Kite.StartElevationDeg - Kite.MinElevationDeg <= Settings.HeliLoopMaxDropDeg)
	{
		return ETrickLoopKind::HeliLoop;
	}
	const float BodyWeightN = Settings.RiderMassKg * KiteUnits::GravityMS2;
	if (Loop.RiderHeightAtStartCm >= Settings.MegaloopMinRiderHeightCm
		&& Kite.MinElevationDeg <= Settings.MegaloopMaxElevationDeg
		&& Kite.PeakTensionN >= Settings.MegaloopMinTensionBodyWeights * BodyWeightN)
	{
		return ETrickLoopKind::Megaloop;
	}
	return ETrickLoopKind::Kiteloop;
}

bool TrickRecognition::IsContraLoop(const FKiteLoopRecord& Loop)
{
	return Loop.Direction * Loop.RiderTravelSide < 0;
}

TArray<FTrickLoop> TrickRecognition::ClassifyLoops(const TArray<FJumpLoop>& Loops, const FLoopClassifySettings& Settings)
{
	// Gaps are measured on the jump's clock, from one record's end to the next one's start.
	const auto GapSeconds = [&Loops](int32 Earlier, int32 Later)
	{
		return Loops[Later].StartSinceTakeoffSeconds
			- (Loops[Earlier].StartSinceTakeoffSeconds + Loops[Earlier].Loop.DurationSeconds);
	};
	const auto IsSHalf = [&Loops, &Settings](int32 Index)
	{
		const FKiteLoopRecord& Kite = Loops[Index].Loop;
		return !Kite.bCompleted && !Kite.bKiteCrashed && Kite.Direction != 0
			&& FMath::Abs(Kite.TurnDeg) >= Settings.SLoopHalfMinDeg;
	};

	TArray<FTrickLoop> Result;
	const int32 Count = Loops.Num();
	for (int32 Index = 0; Index < Count;)
	{
		const FKiteLoopRecord& Kite = Loops[Index].Loop;

		if (IsSHalf(Index))
		{
			// Halves alternating in direction, each starting soon after the last one ended.
			int32 Last = Index;
			while (Last + 1 < Count && IsSHalf(Last + 1)
				&& Loops[Last + 1].Loop.Direction == -Loops[Last].Loop.Direction
				&& GapSeconds(Last, Last + 1) <= Settings.SLoopMaxGapSeconds)
			{
				++Last;
			}
			const int32 Halves = Last - Index + 1;
			if (Halves >= 2)
			{
				FTrickLoop Loop;
				Loop.Kind = Halves >= 3 ? ETrickLoopKind::SnakeLoop : ETrickLoopKind::SLoop;
				Result.Add(Loop);
			}
			Index = Last + 1;
			continue;
		}

		if (!Kite.bCompleted)
		{
			++Index;
			continue;
		}

		const ETrickLoopKind Kind = ClassifyLoop(Loops[Index], Settings);
		if (Kind == ETrickLoopKind::HeliLoop)
		{
			FTrickLoop Loop;
			Loop.Kind = Kind;
			Loop.bContra = IsContraLoop(Kite);
			Result.Add(Loop);
			++Index;
			continue;
		}

		// A chain: completed kite or megaloops one way, back to back.
		int32 Last = Index;
		ETrickLoopKind Strongest = Kind;
		while (Last + 1 < Count)
		{
			const FJumpLoop& Next = Loops[Last + 1];
			if (!Next.Loop.bCompleted || Next.Loop.Direction != Kite.Direction
				|| GapSeconds(Last, Last + 1) > Settings.ChainMaxGapSeconds)
			{
				break;
			}
			const ETrickLoopKind NextKind = ClassifyLoop(Next, Settings);
			if (NextKind == ETrickLoopKind::HeliLoop)
			{
				break;
			}
			if (NextKind == ETrickLoopKind::Megaloop)
			{
				Strongest = ETrickLoopKind::Megaloop;
			}
			++Last;
		}
		for (int32 Member = Index; Member <= Last; ++Member)
		{
			FTrickLoop Loop;
			Loop.Kind = Strongest;
			Loop.bContra = IsContraLoop(Loops[Member].Loop);
			Result.Add(Loop);
		}
		Index = Last + 1;
	}
	return Result;
}

ELoopRollTiming TrickRecognition::LoopRollTiming(float RollStartSeconds, float PeakTensionTimeSeconds, const FLoopClassifySettings& Settings)
{
	const float MarginSeconds = FMath::Max(Settings.RollTimingMarginSeconds, 0.0f);
	if (RollStartSeconds < PeakTensionTimeSeconds && RollStartSeconds <= PeakTensionTimeSeconds - MarginSeconds)
	{
		return ELoopRollTiming::Early;
	}
	if (RollStartSeconds > PeakTensionTimeSeconds && RollStartSeconds >= PeakTensionTimeSeconds + MarginSeconds)
	{
		return ELoopRollTiming::Late;
	}
	return ELoopRollTiming::None;
}

float TrickRecognition::PeakTensionSinceTakeoffSeconds(const FJumpLoop& Loop)
{
	return Loop.StartSinceTakeoffSeconds + (Loop.Loop.PeakTensionTimeSeconds - Loop.Loop.StartTimeSeconds);
}

FTrickSignature TrickRecognition::SignatureFromJump(const FJumpRecord& Record, const FLoopClassifySettings& Settings,
	const FLandingGradeSettings& GradeSettings, float MinGrabHoldSeconds)
{
	FTrickSignature Signature;
	Signature.Loops = ClassifyLoops(Record.Loops, Settings);

	// The rider's rotation, as the recogniser credited it (T1.6).
	Signature.Inversions = Record.Inversions;
	Signature.SpinHalfTurns = FMath::Max(Record.SpinHalfTurns, 0);
	Signature.SpinSense = Signature.SpinHalfTurns > 0 ? Record.SpinSense : ETrickSense::None;
	Signature.LandingStance = Record.LandingStance;

	// Grabs and the one-footer (T2.1, T2.2): a grab counts once it has been held long enough.
	for (const FTrickGrab& Grab : Record.Grabs)
	{
		if (Grab.HoldSeconds >= MinGrabHoldSeconds)
		{
			Signature.Grabs.Add(Grab);
		}
	}
	Signature.bOneFooter = Record.bOneFooter;
	// The board-off (T2.3), as the board-off state credited it.
	Signature.BoardOff = Record.BoardOff;
	Signature.BoardOffSeconds = Record.BoardOff != ETrickBoardOff::None ? Record.BoardOffSeconds : 0.0f;

	// Early or late roll: the first inversion's start against the yank of the first completed kite or
	// megaloop, given to every kite and megaloop entry so a chain still names as one.
	if (Record.Inversions.Num() > 0 && Record.RollStartSinceTakeoffSeconds >= 0.0f)
	{
		const FJumpLoop* Yanking = nullptr;
		for (const FJumpLoop& JumpLoop : Record.Loops)
		{
			if (JumpLoop.Loop.bCompleted && ClassifyLoop(JumpLoop, Settings) != ETrickLoopKind::HeliLoop)
			{
				Yanking = &JumpLoop;
				break;
			}
		}
		if (Yanking)
		{
			const ELoopRollTiming Timing = LoopRollTiming(Record.RollStartSinceTakeoffSeconds, PeakTensionSinceTakeoffSeconds(*Yanking), Settings);
			for (FTrickLoop& Loop : Signature.Loops)
			{
				if (Loop.Kind == ETrickLoopKind::Kiteloop || Loop.Kind == ETrickLoopKind::Megaloop)
				{
					Loop.RollTiming = Timing;
				}
			}
		}
	}
	Signature.Grade = TrickScoring::GradeLanding(Record.LandingYawDeg, Record.LandingG, Record.KiteElevationAtLandingDeg,
		Record.Outcome == EJumpOutcome::Crashed, GradeSettings);
	return Signature;
}
