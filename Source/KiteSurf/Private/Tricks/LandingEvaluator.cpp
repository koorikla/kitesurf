#include "Tricks/LandingEvaluator.h"

float FLandingThresholds::SpeedRetentionFor(ELandingGrade Grade) const
{
	switch (Grade)
	{
	case ELandingGrade::Stomped: return SpeedRetentionStomped;
	case ELandingGrade::Clean:   return SpeedRetentionClean;
	case ELandingGrade::Sketchy: return SpeedRetentionSketchy;
	default:                     return SpeedRetentionCrash;
	}
}

float LandingEvaluator::FoldYawDeg(float YawDeg)
{
	if (!FMath::IsFinite(YawDeg))
	{
		return YawDeg;
	}
	float Yaw = FMath::Fmod(FMath::Abs(YawDeg), 360.0f); // 0..360
	if (Yaw > 180.0f)
	{
		Yaw = 360.0f - Yaw; // 0..180: angle between heading and travel
	}
	if (Yaw > 90.0f)
	{
		Yaw = 180.0f - Yaw; // 0..90: the tail leads, a switch landing
	}
	return Yaw;
}

FLandingVerdict LandingEvaluator::Evaluate(const FLandingInputs& In, const FLandingThresholds& T)
{
	FLandingVerdict Verdict;
	auto Finish = [&T, &Verdict](ELandingGrade Grade, ELandingCause Cause)
	{
		Verdict.Grade = Grade;
		Verdict.Cause = Cause;
		Verdict.SpeedRetention = T.SpeedRetentionFor(Grade);
		return Verdict;
	};

	// The rider was not in a state to ride away, whatever the board did.
	if (!In.bBoardAttached)
	{
		return Finish(ELandingGrade::Crash, ELandingCause::BoardOff);
	}
	if (!In.bBarInHands)
	{
		return Finish(ELandingGrade::Crash, ELandingCause::BarLost);
	}
	if (In.bPassInProgress)
	{
		return Finish(ELandingGrade::Crash, ELandingCause::PassUnfinished);
	}
	// The one-footer (T2.2): a back foot still out of its strap cannot take the landing.
	if (In.BackFoot == EFootStrapState::Out)
	{
		return Finish(ELandingGrade::Crash, ELandingCause::FootOutOfStrap);
	}
	if (!(In.BodyUpDot >= T.InvertedBodyUpDot))
	{
		return Finish(ELandingGrade::Crash, ELandingCause::Inverted);
	}
	// Comparisons are written so that NaN fails the limit (grades down, never up).
	if (!(In.LandingG <= T.CrashLandingG))
	{
		return Finish(ELandingGrade::Crash, ELandingCause::TooHard);
	}

	// The board's attitude: tilt from the water normal, then yaw off the velocity.
	const float Tilt = FMath::Abs(In.TiltDeg);
	const float Yaw = FoldYawDeg(In.YawOffVelocityDeg);
	if (!(Tilt <= T.Sketchy.MaxTiltDeg))
	{
		return Finish(ELandingGrade::Crash, In.ErrorAlongSpin >= 0.0f ? ELandingCause::UnderRotated : ELandingCause::OverRotated);
	}
	if (!(Yaw <= T.Sketchy.MaxYawDeg))
	{
		return Finish(ELandingGrade::Crash, ELandingCause::Sideways);
	}

	ELandingGrade Grade = ELandingGrade::Sketchy;
	if (Tilt <= T.Stomped.MaxTiltDeg && Yaw <= T.Stomped.MaxYawDeg
		&& In.KiteElevationDeg >= T.StompedMinKiteElevationDeg && In.LandingG <= T.StompedMaxLandingG)
	{
		Grade = ELandingGrade::Stomped;
	}
	else if (Tilt <= T.Clean.MaxTiltDeg && Yaw <= T.Clean.MaxYawDeg)
	{
		Grade = ELandingGrade::Clean;
	}

	// A hot or hard landing caps the grade at sketchy, whatever the attitude.
	const bool bKiteLow = !(In.KiteElevationDeg >= T.HotLandingKiteElevationDeg);
	const bool bTooHard = !(In.SinkMS <= T.HotLandingSinkMS) || !(In.LandingG <= T.SketchyMinLandingG);
	if (bKiteLow || bTooHard || In.bHotLanding)
	{
		// The kite is named first: it is what drags a rider off a landing. The board's hot flag on
		// its own (the inputs here did not explain it) is also read as the kite.
		const ELandingCause Cause = bKiteLow ? ELandingCause::KiteTooLow
			: bTooHard ? ELandingCause::TooHard
			: ELandingCause::KiteTooLow;
		// A board caught late, then a foot coming back into its strap, are named before the kite and
		// the g (they are what the rider did wrong; docs/tricks/T2.md T2.6 priority).
		return Finish(ELandingGrade::Sketchy, In.bBoardCaughtLate ? ELandingCause::BoardCaughtLate
			: In.BackFoot == EFootStrapState::Returning ? ELandingCause::FootLate : Cause);
	}
	if (In.bBoardCaughtLate)
	{
		// The board-off's re-catch was in its grace (T2.3): the board is under the feet, just.
		return Finish(ELandingGrade::Sketchy, ELandingCause::BoardCaughtLate);
	}
	if (In.BackFoot == EFootStrapState::Returning)
	{
		// The foot was on its way back in: the rider lands on it, at best sketchy.
		return Finish(ELandingGrade::Sketchy, ELandingCause::FootLate);
	}
	return Finish(Grade, ELandingCause::None);
}

FLandingGeometry LandingEvaluator::ComputeGeometry(const FQuat& BoardQuat, const FVector& WaterNormal, const FVector& Velocity,
	const FQuat& BodyQuat, const FVector& AngularVelocity)
{
	FLandingGeometry Geometry;
	const FVector Normal = WaterNormal.IsNearlyZero() ? FVector::UpVector : WaterNormal.GetSafeNormal();
	const FVector BoardUp = BoardQuat.GetUpVector();
	const FVector BoardForward = BoardQuat.GetForwardVector();

	// atan2 of |cross| and dot rather than acos of the dot: exact near 0 and 180 deg.
	Geometry.TiltDeg = FMath::RadiansToDegrees(FMath::Atan2(FVector::CrossProduct(BoardUp, Normal).Size(), FVector::DotProduct(BoardUp, Normal)));

	// Heading against travel, both along the water plane; |cos| makes either end count.
	const FVector ForwardAlong = FVector::VectorPlaneProject(BoardForward, Normal);
	const FVector VelocityAlong = FVector::VectorPlaneProject(Velocity, Normal);
	constexpr float MinAlongSize = 1.0e-3f;
	if (ForwardAlong.Size() > MinAlongSize && VelocityAlong.Size() > MinAlongSize)
	{
		const FVector F = ForwardAlong.GetSafeNormal();
		const FVector V = VelocityAlong.GetSafeNormal();
		Geometry.YawOffVelocityDeg = FMath::RadiansToDegrees(
			FMath::Atan2(FVector::CrossProduct(F, V).Size(), FMath::Abs(FVector::DotProduct(F, V))));
	}

	Geometry.BodyUpDot = FVector::DotProduct(BodyQuat.GetUpVector(), FVector::UpVector);

	// The axis of the rotation still needed to bring the board's up onto the normal, along the
	// spin: positive means the spin stopped short of it. Unit axes, as FAttitudeDebug::ErrorAlongSpin.
	const FVector ErrorAxis = FVector::CrossProduct(BoardUp, Normal).GetSafeNormal();
	if (!AngularVelocity.IsNearlyZero() && !ErrorAxis.IsZero())
	{
		Geometry.ErrorAlongSpin = FVector::DotProduct(ErrorAxis, AngularVelocity.GetSafeNormal());
	}
	return Geometry;
}

void LandingEvaluator::ApplyGeometry(const FLandingGeometry& Geometry, FLandingInputs& Inputs)
{
	Inputs.TiltDeg = Geometry.TiltDeg;
	Inputs.YawOffVelocityDeg = Geometry.YawOffVelocityDeg;
	Inputs.BodyUpDot = Geometry.BodyUpDot;
	Inputs.ErrorAlongSpin = Geometry.ErrorAlongSpin;
}
