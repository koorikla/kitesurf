#include "Tricks/GrabState.h"

namespace GrabStateLocal
{
	/** Rounding slack on the linear ramps, so a whole number of steps reaches the end exactly. */
	constexpr float StepEpsilon = 1.0e-4f;
}

void FGrabState::Reset()
{
	const FGrabStateTuning Kept = Tuning;
	const FBoardOffTuning KeptBoardOff = BoardOff.Tuning;
	*this = FGrabState(Kept);
	BoardOff.Tuning = KeptBoardOff;
}

ETrickGrabZone FGrabState::ResolveZone(const FVector2D& ZoneStick, float Deadzone)
{
	if (ZoneStick.Size() <= Deadzone)
	{
		return ETrickGrabZone::ToeEdge;
	}
	if (FMath::Abs(ZoneStick.Y) >= FMath::Abs(ZoneStick.X))
	{
		return ZoneStick.Y > 0.0f ? ETrickGrabZone::Nose : ETrickGrabZone::Tail;
	}
	// X +1 is towards the rider's back (the heel edge), -1 towards the chest (the toe edge).
	return ZoneStick.X < 0.0f ? ETrickGrabZone::ToeEdge : ETrickGrabZone::HeelEdge;
}

float FGrabState::TuckForZone(ETrickGrabZone Zone)
{
	switch (Zone)
	{
	case ETrickGrabZone::Nose:
	case ETrickGrabZone::Tail:
		return 0.8f;
	default:
		return 1.0f;
	}
}

float FGrabState::TorsoFoldDegForZone(ETrickGrabZone Zone)
{
	switch (Zone)
	{
	case ETrickGrabZone::ToeEdge:
	case ETrickGrabZone::BehindToe:
		return 55.0f;
	case ETrickGrabZone::Nose:
	case ETrickGrabZone::Tail:
		return 40.0f;
	case ETrickGrabZone::HeelEdge:
	case ETrickGrabZone::BehindHeel:
	default:
		return 15.0f;
	}
}

float FGrabState::GetReachWeight() const
{
	return FMath::SmoothStep(0.0f, 1.0f, Reach);
}

float FGrabState::GetPrevReachWeight() const
{
	return FMath::SmoothStep(0.0f, 1.0f, PrevReach);
}

float FGrabState::GetTuckTarget() const
{
	const float GrabTuck = Phase == EPhase::OnBar ? 0.0f : TuckForZone(Zone) * GetReachWeight();
	return FMath::Max(GrabTuck, BoardOff.GetTuckTarget());
}

EFootStrapState FGrabState::GetFootAtTouchdown() const
{
	if (FootOut <= 0.0f)
	{
		return EFootStrapState::In;
	}
	return FootOut >= Tuning.FootOutAtTouchdown ? EFootStrapState::Out : EFootStrapState::Returning;
}

void FGrabState::StartReach(ETrickHand InHand, const FVector2D& Stick)
{
	// From the bar, or from wherever a hand going back has got to.
	Phase = EPhase::Reaching;
	Hand = InHand;
	StickPeak = Stick;
	Zone = ResolveZone(StickPeak, Tuning.StickDeadzone);
	HoldSeconds = 0.0f;
	OpenGrab = INDEX_NONE;
}

void FGrabState::EndGrab()
{
	if (Phase == EPhase::Reaching)
	{
		// Let go before the hand got there: logged, with no hold.
		FTrickGrab& Grab = Grabs.AddDefaulted_GetRef();
		Grab.Hand = Hand;
		Grab.Zone = Zone;
		Grab.HoldSeconds = 0.0f;
	}
	else if (Phase == EPhase::Holding && Grabs.IsValidIndex(OpenGrab))
	{
		Grabs[OpenGrab].HoldSeconds = HoldSeconds;
	}
	OpenGrab = INDEX_NONE;
	Phase = EPhase::Returning;
}

void FGrabState::Step(const FGrabStateInput& In, float Dt)
{
	PrevReach = Reach;
	PrevFootOut = FootOut;
	bArrivedThisStep = false;
	Dt = FMath::Max(Dt, 0.0f);

	if (In.bAirborne && !bWasAirborne)
	{
		// A new flight: its own grabs and one-foot time.
		Grabs.Reset();
		OpenGrab = INDEX_NONE;
		OneFootSeconds = 0.0f;
	}

	// Fresh presses: a button already held when the rider left the water does not grab.
	const bool bFrontPressed = In.bFront && !bFrontWasHeld;
	const bool bBackPressed = In.bBack && !bBackWasHeld;
	bFrontWasHeld = In.bFront;
	bBackWasHeld = In.bBack;
	bWasAirborne = In.bAirborne;
	bZoneStickActive = In.bAirborne && (In.bFront || In.bBack);

	// The board-off (T2.3): both buttons held, made this step by a fresh press of either.
	FBoardOffInput BoardOffIn;
	BoardOffIn.bChord = In.bFront && In.bBack;
	BoardOffIn.bChordPressed = BoardOffIn.bChord && (bFrontPressed || bBackPressed);
	BoardOffIn.bAirborne = In.bAirborne;
	BoardOffIn.Stick = In.ZoneStick;
	BoardOff.Step(BoardOffIn, Dt);

	const bool bHolding = Phase == EPhase::Reaching || Phase == EPhase::Holding;
	if (bHolding)
	{
		// The grab lasts as long as its own button is held in the air, and until the board comes off.
		const bool bOwnHeld = Hand == ETrickHand::Front ? In.bFront : In.bBack;
		if (!In.bAirborne || !bOwnHeld || BoardOff.IsBoardOff())
		{
			EndGrab();
		}
	}
	else if (In.bAirborne && !BoardOff.IsBoardOff())
	{
		// One hand at a time. A press with the other button held, or both at once, is the board-off's
		// chord (above), not a grab.
		if (bFrontPressed && !In.bBack)
		{
			StartReach(ETrickHand::Front, In.ZoneStick);
		}
		else if (bBackPressed && !In.bFront)
		{
			StartReach(ETrickHand::Back, In.ZoneStick);
		}
	}

	switch (Phase)
	{
	case EPhase::Reaching:
		// The strongest push of the stick during the reach picks the zone; it is latched on arrival.
		if (In.ZoneStick.SizeSquared() > StickPeak.SizeSquared())
		{
			StickPeak = In.ZoneStick;
		}
		Zone = ResolveZone(StickPeak, Tuning.StickDeadzone);
		Reach = Reach + Dt / FMath::Max(Tuning.ReachSeconds, KINDA_SMALL_NUMBER);
		if (Reach >= 1.0f - GrabStateLocal::StepEpsilon)
		{
			Reach = 1.0f;
			Phase = EPhase::Holding;
			HoldSeconds = 0.0f;
			bArrivedThisStep = true;
			FTrickGrab& Grab = Grabs.AddDefaulted_GetRef();
			Grab.Hand = Hand;
			Grab.Zone = Zone;
			Grab.HoldSeconds = 0.0f;
			OpenGrab = Grabs.Num() - 1;
		}
		break;
	case EPhase::Holding:
		HoldSeconds += Dt;
		if (Grabs.IsValidIndex(OpenGrab))
		{
			Grabs[OpenGrab].HoldSeconds = HoldSeconds;
		}
		break;
	case EPhase::Returning:
		Reach = Reach - Dt / FMath::Max(Tuning.ReturnSeconds, KINDA_SMALL_NUMBER);
		if (Reach <= GrabStateLocal::StepEpsilon)
		{
			Reach = 0.0f;
			Phase = EPhase::OnBar;
		}
		break;
	case EPhase::OnBar:
	default:
		break;
	}

	// The one-footer: the back foot out while held in the air, back in when let go or on the water.
	const bool bFootOutWanted = In.bOneFoot && In.bAirborne;
	if (bFootOutWanted)
	{
		FootOut = FootOut + Dt / FMath::Max(Tuning.FootOutSeconds, KINDA_SMALL_NUMBER);
		FootOut = FootOut >= 1.0f - GrabStateLocal::StepEpsilon ? 1.0f : FootOut;
	}
	else
	{
		FootOut = FootOut - Dt / FMath::Max(Tuning.FootReturnSeconds, KINDA_SMALL_NUMBER);
		FootOut = FootOut <= GrabStateLocal::StepEpsilon ? 0.0f : FootOut;
	}
	if (In.bAirborne && FootOut >= 1.0f)
	{
		OneFootSeconds += Dt;
	}
}
