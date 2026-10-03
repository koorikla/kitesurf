#include "Tricks/BarState.h"

// Helpers in an anonymous namespace inside BarStateMachine, so a unity build cannot merge them
// with helpers of the same name in other files.
namespace BarStateMachine
{
namespace
{
	/** Slack on timer comparisons, so a sum of equal steps that lands on a limit counts as on it. */
	constexpr float TimerEpsSeconds = 1e-4f;

	ETrickSense SenseOf(float Sign)
	{
		return Sign >= 0.0f ? ETrickSense::Backside : ETrickSense::Frontside;
	}

	float SignOf(ETrickSense Sense)
	{
		switch (Sense)
		{
		case ETrickSense::Backside:  return 1.0f;
		case ETrickSense::Frontside: return -1.0f;
		default:                     return 0.0f;
		}
	}

	void LoseBar(FBarState& S, FBarEvents& E, EBarLossCause Cause)
	{
		S.Place = EBarPlace::Lost;
		S.Hands = EBarHands::None;
		S.LossCause = Cause;
		S.PassT = 0.0f;
		E.Lost = Cause;
	}

	/** Updates W from the line's azimuth, unless the line runs along the body. */
	void UpdateWrap(FBarState& S, const FBarInputs& In, const FBarTunables& T)
	{
		const FVector Up = In.Body.GetAxisZ();
		const FVector Dir = In.LineDirWorld.GetSafeNormal();
		if (Dir.IsZero() || FMath::Abs(FVector::DotProduct(Up, Dir)) >= T.WrapHoldUpDot)
		{
			return;
		}
		const float Azimuth = LineAzimuthDeg(In.Body, Dir);
		if (S.bHasLineAngle)
		{
			// Turning backside, the chest goes towards the tail, so the line moves towards the
			// nose in the body frame: the azimuth grows by NoseSideSign per degree of backside.
			const float NoseSide = In.NoseSideSign >= 0.0f ? 1.0f : -1.0f;
			const float DeltaW = NoseSide * FMath::FindDeltaAngleDegrees(S.LastLineAngleDeg, Azimuth);
			if (S.WrapDeg < 90.0f && S.WrapDeg + DeltaW >= 90.0f)
			{
				S.bRouteBehind = true;
			}
			S.WrapDeg += DeltaW;
			if (FMath::Abs(S.WrapDeg) < 90.0f)
			{
				S.bRouteBehind = false;
			}
		}
		S.LastLineAngleDeg = Azimuth;
		S.bHasLineAngle = true;
	}
} // namespace
} // namespace BarStateMachine

float BarStateMachine::LineAzimuthDeg(const FQuat& Body, const FVector& LineDir)
{
	const FVector Local = Body.UnrotateVector(LineDir);
	if (FMath::IsNearlyZero(Local.X) && FMath::IsNearlyZero(Local.Y))
	{
		return 0.0f;
	}
	return FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));
}

float BarStateMachine::BackToKiteDeg(const FQuat& Body, const FVector& LineDir)
{
	const FVector Local = Body.UnrotateVector(LineDir);
	const FVector2D Flat(Local.X, Local.Y);
	if (Flat.SizeSquared() < KINDA_SMALL_NUMBER)
	{
		return 90.0f;
	}
	// The back points along -Front: the angle between -Front and the line in the Front/Right plane.
	const float Cos = FMath::Clamp(-Flat.X / Flat.Size(), -1.0f, 1.0f);
	return FMath::RadiansToDegrees(FMath::Acos(Cos));
}

FBarEvents BarStateMachine::Step(FBarState& S, const FBarInputs& In, const FBarTunables& T, float Dt)
{
	FBarEvents E;
	Dt = FMath::Max(Dt, 0.0f);

	// Take-off starts a new jump; the wrap there is the reference for the pass degrees.
	if (In.bAirborne && !S.bWasAirborne)
	{
		S.WrapAtTakeoffDeg = S.WrapDeg;
		S.JumpPasses.Reset();
	}
	S.bWasAirborne = In.bAirborne;

	if (S.Place == EBarPlace::Lost)
	{
		return E;
	}

	const float TensionBW = In.TensionN / FMath::Max(In.BodyWeightN, 1.0f);

	// Hook in or out: only on the water, with both hands on the bar in front and the lines not
	// round the body (a toeside rider unwinds first).
	if (In.bHookPressed && In.bOnWaterRideable && !In.bAirborne && S.Place == EBarPlace::Front
		&& S.Hands == EBarHands::Both && FMath::Abs(S.WrapDeg) < 90.0f)
	{
		S.bHooked = !S.bHooked;
		if (S.bHooked)
		{
			E.bHooked = true;
		}
		else
		{
			E.bUnhooked = true;
		}
	}
	if (S.bHooked)
	{
		// The harness takes the load and the bar stays in front: nothing to wrap, grip or pass.
		S.WrapDeg = 0.0f;
		S.bRouteBehind = false;
		S.bHasLineAngle = false;
		S.OverGripSeconds = 0.0f;
		S.SlackSeconds = 0.0f;
		S.PassBufferLeft = 0.0f;
		return E;
	}

	// Grip limit.
	S.OverGripSeconds = TensionBW > T.GripLimitBW ? S.OverGripSeconds + Dt : 0.0f;
	if (S.OverGripSeconds > T.GripLimitSeconds + TimerEpsSeconds)
	{
		LoseBar(S, E, EBarLossCause::OverGrip);
		return E;
	}

	// Wrap and the bar's place.
	UpdateWrap(S, In, T);
	if (S.Place != EBarPlace::Passing)
	{
		S.Place = (FMath::Abs(S.WrapDeg) >= 90.0f && S.bRouteBehind) ? EBarPlace::BehindBack : EBarPlace::Front;
	}

	// Pass.
	const bool bSlack = TensionBW < T.PassSlackTensionBW;
	S.SlackSeconds = bSlack ? S.SlackSeconds + Dt : 0.0f;
	S.PassBufferLeft = In.bPassPressed ? T.PassRequestBufferSeconds : FMath::Max(S.PassBufferLeft - Dt, 0.0f);
	if (S.Place != EBarPlace::Passing && S.PassBufferLeft > 0.0f && bSlack
		&& BackToKiteDeg(In.Body, In.LineDirWorld) <= T.PassBackToKiteDeg)
	{
		S.Place = EBarPlace::Passing;
		S.Hands = EBarHands::None;
		S.PassT = 0.0f;
		S.PassWaterSeconds = 0.0f;
		S.PassSense = S.WrapDeg >= 0.0f ? 1.0f : -1.0f;
		S.PassBufferLeft = 0.0f;
		E.bPassStarted = true;
	}
	if (S.Place == EBarPlace::Passing)
	{
		if (TensionBW > T.PassLoseTensionBW)
		{
			LoseBar(S, E, EBarLossCause::PassUnderLoad);
			return E;
		}
		S.PassT += T.PassDurationSeconds > 0.0f ? Dt / T.PassDurationSeconds : 1.0f;
		if (!In.bAirborne)
		{
			S.PassWaterSeconds += Dt;
		}
		if (S.PassT >= 1.0f - TimerEpsSeconds)
		{
			// The bar is back in both hands at the same place behind the back, but the lines now
			// run round the other side.
			S.WrapDeg -= 360.0f * S.PassSense;
			S.bRouteBehind = true;
			S.Place = EBarPlace::BehindBack;
			S.Hands = EBarHands::Both;
			S.PassT = 0.0f;
			E.bPassDone = true;
			E.PassKind = In.bAirborne ? ETrickPassKind::Air : ETrickPassKind::Surface;
			FBarPassRecord Record;
			Record.Sense = SenseOf(S.PassSense);
			Record.Kind = E.PassKind;
			S.JumpPasses.Add(Record);
		}
		else if (S.PassWaterSeconds > T.SurfacePassGraceSeconds + TimerEpsSeconds)
		{
			LoseBar(S, E, EBarLossCause::PassUnfinished);
			return E;
		}
	}

	// Wrapped lines on the water: the rider cannot ride away.
	if (S.Place != EBarPlace::Passing && !In.bAirborne && FMath::Abs(S.WrapDeg) >= T.WrappedLandDeg)
	{
		LoseBar(S, E, EBarLossCause::LinesWrapped);
		return E;
	}

	// One hand off while the bar is in front (unhooked grabs, the tantrum's back hand).
	if (S.Place == EBarPlace::Front)
	{
		if (In.bReleaseBack)
		{
			S.Hands = EBarHands::FrontOnly;
		}
		else if (In.bReleaseFront)
		{
			S.Hands = EBarHands::BackOnly;
		}
		else
		{
			S.Hands = EBarHands::Both;
		}
	}
	return E;
}

float BarStateMachine::EffectiveWrapDeg(const FBarState& S)
{
	return S.Place == EBarPlace::Passing ? S.WrapDeg - 360.0f * S.PassSense : S.WrapDeg;
}

bool BarStateMachine::CanLandRideable(const FBarState& S, const FBarTunables& T)
{
	return S.Place != EBarPlace::Lost && FMath::Abs(EffectiveWrapDeg(S)) < T.WrappedLandDeg;
}

ETrickStance BarStateMachine::StanceForWrap(float WrapDeg, bool bRouteBehind)
{
	if (FMath::Abs(FRotator::NormalizeAxis(WrapDeg)) < 90.0f)
	{
		return ETrickStance::Heelside;
	}
	return bRouteBehind ? ETrickStance::Blind : ETrickStance::Toeside;
}

FBarJumpSummary BarStateMachine::SummariseJump(const FBarState& S, const FBarTunables& T)
{
	FBarJumpSummary Out;
	// Read as it stands: a pass still under way is not counted yet (call this once it has settled).
	const float LandWrap = S.WrapDeg;
	Out.WrapChangeDeg = LandWrap - S.WrapAtTakeoffDeg;
	Out.LandingStance = StanceForWrap(LandWrap, S.bRouteBehind);
	Out.bWrapped = FMath::Abs(LandWrap) >= T.WrappedLandDeg
		|| (S.Place == EBarPlace::Lost && S.LossCause == EBarLossCause::LinesWrapped);

	float SenseSum = 0.0f;
	for (const FBarPassRecord& Pass : S.JumpPasses)
	{
		SenseSum += SignOf(Pass.Sense);
	}
	Out.PassSpinDeg = 360.0f * SenseSum + Out.WrapChangeDeg;
	Out.PassHalfTurns = FMath::RoundToInt(FMath::Abs(Out.PassSpinDeg) / 180.0f);

	// Split the half turns over the passes: two (a 360) to each pass but the last, which takes
	// the rest, keeping at least one half turn for each later pass when there are enough.
	const int32 Count = S.JumpPasses.Num();
	int32 Remaining = Out.PassHalfTurns;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const int32 Later = Count - 1 - Index;
		const int32 Half = Later == 0 ? Remaining : FMath::Min(2, FMath::Max(Remaining - Later, 0));
		Remaining -= Half;
		FTrickPass Pass;
		Pass.Sense = S.JumpPasses[Index].Sense;
		Pass.Kind = S.JumpPasses[Index].Kind;
		Pass.Degrees = Half * 180;
		Out.Passes.Add(Pass);
	}
	return Out;
}

void BarStateMachine::ApplyToSignature(const FBarState& S, FTrickSignature& Signature, const FBarTunables& T)
{
	ApplySummaryToSignature(S.bHooked, S.bHooked ? FBarJumpSummary() : SummariseJump(S, T), Signature);
}

void BarStateMachine::ApplySummaryToSignature(bool bHooked, const FBarJumpSummary& Summary, FTrickSignature& Signature)
{
	Signature.bHooked = bHooked;
	if (bHooked)
	{
		Signature.Passes.Reset();
		return;
	}
	Signature.Passes = Summary.Passes;
	Signature.LandingStance = Summary.LandingStance;
}

ELandingCause BarStateMachine::LandingCauseOf(EBarLossCause Cause)
{
	switch (Cause)
	{
	case EBarLossCause::None:           return ELandingCause::None;
	case EBarLossCause::PassUnfinished: return ELandingCause::PassUnfinished;
	default:                            return ELandingCause::BarLost;
	}
}
