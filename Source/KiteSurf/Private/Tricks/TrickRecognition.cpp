#include "Tricks/TrickRecognition.h"
#include "KiteSurfUnits.h"

ETrickLoopKind TrickRecognition::ClassifyLoop(const FJumpLoop& Loop, const FLoopClassifySettings& Settings)
{
	const FKiteLoopRecord& Kite = Loop.Loop;
	if (Loop.StartSinceApexSeconds > 0.0f && Kite.MinElevationDeg >= Settings.HeliLoopMinElevationDeg)
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

FTrickSignature TrickRecognition::SignatureFromJump(const FJumpRecord& Record, const FLoopClassifySettings& Settings,
	const FLandingGradeSettings& GradeSettings)
{
	FTrickSignature Signature;
	for (const FJumpLoop& JumpLoop : Record.Loops)
	{
		if (!JumpLoop.Loop.bCompleted)
		{
			continue;
		}
		FTrickLoop Loop;
		Loop.Kind = ClassifyLoop(JumpLoop, Settings);
		Loop.bContra = JumpLoop.Loop.Direction * JumpLoop.Loop.RiderTravelSide < 0;
		Signature.Loops.Add(Loop);
	}
	Signature.Grade = TrickScoring::GradeLanding(Record.LandingYawDeg, Record.LandingG, Record.KiteElevationAtLandingDeg,
		Record.Outcome == EJumpOutcome::Crashed, GradeSettings);
	return Signature;
}
