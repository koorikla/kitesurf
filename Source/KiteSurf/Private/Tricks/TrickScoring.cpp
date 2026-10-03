#include "Tricks/TrickScoring.h"
#include "KiteSurfUnits.h"

ELandingGrade TrickScoring::GradeLanding(float YawDeg, float LandingG, float KiteElevationDeg, bool bCrashed,
	const FLandingGradeSettings& Settings)
{
	if (bCrashed)
	{
		return ELandingGrade::Crash;
	}
	const float Yaw = FMath::Abs(YawDeg);
	if (Yaw <= Settings.StompedMaxYawDeg && LandingG <= Settings.StompedMaxG && KiteElevationDeg >= Settings.HotKiteElevationDeg)
	{
		return ELandingGrade::Stomped;
	}
	if (KiteElevationDeg < Settings.HotKiteElevationDeg || LandingG > Settings.SketchyMinG)
	{
		return ELandingGrade::Sketchy;
	}
	return ELandingGrade::Clean;
}

float TrickScoring::ExecutionFactor(ELandingGrade Grade, const FTrickScoringSettings& Settings)
{
	switch (Grade)
	{
	case ELandingGrade::Stomped: return Settings.ExecutionStomped;
	case ELandingGrade::Clean:   return Settings.ExecutionClean;
	case ELandingGrade::Sketchy: return Settings.ExecutionSketchy;
	default:                     return Settings.ExecutionCrash;
	}
}

FTrickScore TrickScoring::ScoreJump(const FJumpRecord& Record, const FTrickSignature& Signature, const FTrickScoringSettings& Settings)
{
	FTrickScore Score;

	const float ApexM = FMath::Max(KiteUnits::CmToM(Record.ApexHeightCm), 0.0f);
	Score.Height = FMath::Pow(ApexM, Settings.HeightExponent);

	// Extremity: each completed loop pays for how low the kite went and how late in the jump it
	// started (rider height at the start over the apex), so a loop thrown at the top with the
	// kite down near the water scores most.
	for (const FJumpLoop& JumpLoop : Record.Loops)
	{
		if (!JumpLoop.Loop.bCompleted)
		{
			continue;
		}
		const float Lowness = Settings.LownessRefDeg > 0.0f
			? FMath::Clamp(1.0f - JumpLoop.Loop.MinElevationDeg / Settings.LownessRefDeg, 0.0f, 1.0f)
			: 0.0f;
		const float Lateness = Record.ApexHeightCm > 0.0f
			? FMath::Clamp(JumpLoop.RiderHeightAtStartCm / Record.ApexHeightCm, 0.0f, 1.0f)
			: 0.0f;
		Score.Extremity += Settings.LoopExtremity * Lowness * Lateness;
	}
	for (const FTrickLoop& Loop : Signature.Loops)
	{
		if (Loop.bContra || Loop.Kind == ETrickLoopKind::SLoop)
		{
			Score.Extremity += Settings.ContraOrSLoopBonus;
			break;
		}
	}

	// Technicality: the element table. Pass rotation counts as spin as well as the pass itself.
	int32 HalfTurns = FMath::Max(Signature.SpinHalfTurns, 0);
	for (const FTrickPass& Pass : Signature.Passes)
	{
		HalfTurns += FMath::RoundToInt(FMath::Abs(static_cast<float>(Pass.Degrees)) / 180.0f);
	}
	float Technicality = Settings.Inversion * Signature.Inversions.Num()
		+ Settings.SpinPer180 * HalfTurns
		+ Settings.Pass * Signature.Passes.Num();
	for (const FTrickGrab& Grab : Signature.Grabs)
	{
		if (Grab.HoldSeconds < Settings.GrabMinHoldSeconds)
		{
			continue;
		}
		const float HoldRange = Settings.GrabFullHoldSeconds - Settings.GrabMinHoldSeconds;
		const float HoldAlpha = HoldRange > 0.0f ? FMath::Clamp((Grab.HoldSeconds - Settings.GrabMinHoldSeconds) / HoldRange, 0.0f, 1.0f) : 1.0f;
		Technicality += Settings.GrabBase + Settings.GrabHoldBonusMax * HoldAlpha;
	}
	if (Signature.bOneFooter)
	{
		Technicality += Settings.OneFooter;
	}
	if (Signature.BoardOff != ETrickBoardOff::None)
	{
		Technicality += Settings.BoardOff;
	}
	if (!Signature.bHooked)
	{
		Technicality += Settings.Unhooked;
	}
	if (Signature.LandingStance != ETrickStance::Heelside)
	{
		Technicality += Settings.BlindOrToesideLanding;
	}
	Score.Technicality = Technicality;

	Score.Execution = Record.Outcome == EJumpOutcome::Crashed ? 0.0f : ExecutionFactor(Signature.Grade, Settings);

	// A crash scores exactly nothing, whatever the rest came to.
	Score.Total = Score.Execution > 0.0f
		? Score.Height * (1.0f + Score.Extremity) * (1.0f + Score.Technicality) * Score.Execution
		: 0.0f;
	return Score;
}

float TrickScoring::RepeatFactor(int32 PriorCount, const FTrickScoringSettings& Settings)
{
	if (Settings.RepeatFactors.Num() == 0)
	{
		return 1.0f;
	}
	const int32 Index = FMath::Clamp(PriorCount, 0, Settings.RepeatFactors.Num() - 1);
	return Settings.RepeatFactors[Index];
}

float FTrickSession::Add(const FString& FamilyKey, float RawTotal, const FTrickScoringSettings& Settings)
{
	int32& Count = Counts.FindOrAdd(FamilyKey);
	const float Factor = TrickScoring::RepeatFactor(Count, Settings);
	++Count;
	return RawTotal * Factor;
}

float FTrickSession::NextRepeatFactor(const FString& FamilyKey, const FTrickScoringSettings& Settings) const
{
	return TrickScoring::RepeatFactor(GetCount(FamilyKey), Settings);
}

int32 FTrickSession::GetCount(const FString& FamilyKey) const
{
	const int32* Count = Counts.Find(FamilyKey);
	return Count ? *Count : 0;
}
