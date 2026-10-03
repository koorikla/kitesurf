#include "Tricks/BoardOffState.h"
#include "Tricks/BoardGrabPoints.h"

namespace BoardOffStateLocal
{
	/** Rounding slack on the linear ramps, so a whole number of steps reaches the end exactly. */
	constexpr float StepEpsilon = 1.0e-4f;
}

void FBoardOffState::Reset()
{
	const FBoardOffTuning Kept = Tuning;
	*this = FBoardOffState(Kept);
}

ETrickBoardOff FBoardOffState::ResolveVariant(const FVector2D& Stick, float Deadzone)
{
	if (Stick.Size() <= Deadzone)
	{
		return ETrickBoardOff::Plain;
	}
	if (FMath::Abs(Stick.Y) >= FMath::Abs(Stick.X))
	{
		return Stick.Y > 0.0f ? ETrickBoardOff::Superman : ETrickBoardOff::TicTac;
	}
	return ETrickBoardOff::BoardPass;
}

ETrickBoardOff FBoardOffState::CreditedVariant(ETrickBoardOff InVariant, float InTicTacDeg, float InPassPhase, const FBoardOffTuning& InTuning)
{
	if (InVariant == ETrickBoardOff::TicTac && InTicTacDeg < InTuning.TicTacMinDeg)
	{
		return ETrickBoardOff::Plain;
	}
	if (InVariant == ETrickBoardOff::BoardPass && InPassPhase < InTuning.BoardPassMinPhase)
	{
		return ETrickBoardOff::Plain;
	}
	return InVariant;
}

float FBoardOffState::GetOffWeight() const
{
	return FMath::SmoothStep(0.0f, 1.0f, Off);
}

float FBoardOffState::GetPrevOffWeight() const
{
	return FMath::SmoothStep(0.0f, 1.0f, PrevOff);
}

float FBoardOffState::GetCatchSecondsLeft() const
{
	return Phase == EPhase::Catching ? Off * Tuning.RecatchSeconds : 0.0f;
}

EBoardCatchState FBoardOffState::GetCatchAtTouchdown() const
{
	switch (Phase)
	{
	case EPhase::Attached:
		return EBoardCatchState::Attached;
	case EPhase::Catching:
		// The last RecatchGraceSeconds of the re-catch: the feet are nearly in, the landing is sketchy.
		return GetCatchSecondsLeft() <= Tuning.RecatchGraceSeconds + BoardOffStateLocal::StepEpsilon ? EBoardCatchState::CaughtLate : EBoardCatchState::NotCaught;
	case EPhase::Removing:
	case EPhase::Held:
	default:
		return EBoardCatchState::NotCaught;
	}
}

float FBoardOffState::GetTuckTarget() const
{
	if (Phase == EPhase::Attached)
	{
		return 0.0f;
	}
	float Tuck = Tuning.TuckPlain;
	switch (Variant)
	{
	case ETrickBoardOff::Superman:  Tuck = Tuning.TuckSuperman; break;
	case ETrickBoardOff::TicTac:    Tuck = Tuning.TuckTicTac; break;
	case ETrickBoardOff::BoardPass: Tuck = Tuning.TuckBoardPass; break;
	default: break;
	}
	return FMath::Clamp(Tuck, 0.0f, 1.0f) * GetOffWeight();
}

void FBoardOffState::StartRemoving(const FVector2D& Stick)
{
	// From the feet, or from wherever a re-catch has got to.
	Phase = EPhase::Removing;
	StickPeak = Stick;
	Variant = ResolveVariant(StickPeak, Tuning.StickDeadzone);
	HeldSeconds = 0.0f;
	TicTacDeg = 0.0f;
	PrevTicTacDeg = 0.0f;
	PassPhase = 0.0f;
	PrevPassPhase = 0.0f;
}

void FBoardOffState::Step(const FBoardOffInput& In, float Dt)
{
	PrevOff = Off;
	PrevTicTacDeg = TicTacDeg;
	PrevPassPhase = PassPhase;
	bCaughtThisStep = false;
	Dt = FMath::Max(Dt, 0.0f);

	if (In.bAirborne && !bWasAirborne)
	{
		// A new flight: its own board-off.
		FlightVariant = ETrickBoardOff::None;
		FlightSeconds = 0.0f;
	}
	bWasAirborne = In.bAirborne;

	const bool bOff = Phase == EPhase::Removing || Phase == EPhase::Held;
	if (!In.bAirborne)
	{
		// On the water nothing starts, and a board still off goes back to the feet: the landing has
		// been graded already (a crash, if it was not caught).
		if (bOff)
		{
			Phase = EPhase::Catching;
		}
	}
	else if (In.bChord && In.bChordPressed && (Phase == EPhase::Attached || Phase == EPhase::Catching))
	{
		StartRemoving(In.Stick);
	}
	else if (bOff && !In.bChord)
	{
		// Let go: the board comes back under the feet.
		Phase = EPhase::Catching;
	}

	switch (Phase)
	{
	case EPhase::Removing:
		// The strongest push of the stick during the removal picks the variant; it is latched on arrival.
		if (In.Stick.SizeSquared() > StickPeak.SizeSquared())
		{
			StickPeak = In.Stick;
		}
		Variant = ResolveVariant(StickPeak, Tuning.StickDeadzone);
		Off = Off + Dt / FMath::Max(Tuning.RemoveSeconds, KINDA_SMALL_NUMBER);
		if (Off >= 1.0f - BoardOffStateLocal::StepEpsilon)
		{
			Off = 1.0f;
			Phase = EPhase::Held;
			HeldSeconds = 0.0f;
		}
		break;
	case EPhase::Held:
	{
		HeldSeconds += Dt;
		FlightSeconds += Dt;
		if (Variant == ETrickBoardOff::TicTac)
		{
			TicTacDeg = 360.0f * FMath::SmoothStep(0.0f, 1.0f, HeldSeconds / FMath::Max(Tuning.TicTacSpinSeconds, KINDA_SMALL_NUMBER));
		}
		else if (Variant == ETrickBoardOff::BoardPass)
		{
			PassPhase = FMath::SmoothStep(0.0f, 1.0f, HeldSeconds / FMath::Max(Tuning.BoardPassSeconds, KINDA_SMALL_NUMBER));
		}
		if (HeldSeconds >= Tuning.MinOffSeconds - BoardOffStateLocal::StepEpsilon)
		{
			FlightVariant = CreditedVariant(Variant, TicTacDeg, PassPhase, Tuning);
		}
		break;
	}
	case EPhase::Catching:
		Off = Off - Dt / FMath::Max(Tuning.RecatchSeconds, KINDA_SMALL_NUMBER);
		if (Off <= BoardOffStateLocal::StepEpsilon)
		{
			Off = 0.0f;
			Phase = EPhase::Attached;
			bCaughtThisStep = true;
			HeldSeconds = 0.0f;
			TicTacDeg = 0.0f;
			PassPhase = 0.0f;
		}
		break;
	case EPhase::Attached:
	default:
		break;
	}
}

FTransform BoardOffPose::RiderFrame(const FVector& Pelvis, const FQuat& Body)
{
	return FTransform(Body.GetNormalized(), Pelvis);
}

FTransform BoardOffPose::BlendBoard(const FTransform& Strapped, const FTransform& Held, float Weight)
{
	const float W = FMath::Clamp(Weight, 0.0f, 1.0f);
	FTransform Result = Strapped;
	Result.SetLocation(FMath::Lerp(Strapped.GetLocation(), Held.GetLocation(), W));
	Result.SetRotation(FQuat::Slerp(Strapped.GetRotation(), Held.GetRotation(), W).GetNormalized());
	return Result;
}

FBoardOffPose BoardOffPose::Evaluate(ETrickBoardOff Variant, float TicTacDeg, float PassPhase, float NoseSideSign)
{
	// The rider frame: Front, Right, Up. The nose is on the front hand's side.
	const float N = NoseSideSign >= 0.0f ? 1.0f : -1.0f;
	const FVector Front = FVector::ForwardVector;
	const FVector Right = FVector::RightVector;
	const FVector Up = FVector::UpVector;
	const FVector NoseSide = Right * N;
	const FVector BackHandSide = -NoseSide;
	constexpr int32 FrontHand = static_cast<int32>(ETrickHand::Front);
	constexpr int32 BackHand = static_cast<int32>(ETrickHand::Back);

	FBoardOffPose Pose;
	// Knees tucked up under the board by default: the ankles below the hips, a little forwards. Estimates.
	auto SetAnkles = [&Pose](const FVector& Left)
	{
		Pose.AnkleInRider[0] = Left;
		Pose.AnkleInRider[1] = FVector(Left.X, -Left.Y, Left.Z);
	};
	SetAnkles(FVector(10.0f, -RiderRig::HipHalfWidthCm, -55.0f));

	switch (Variant)
	{
	case ETrickBoardOff::Superman:
	{
		// Out in front, deck towards the rider, so the toe rail is on top; the back hand holds its tail end.
		const FQuat Rotation = FRotationMatrix::MakeFromXZ(NoseSide, -Front).ToQuat();
		Pose.BoardInRider = FTransform(Rotation, FVector(36.0f, 0.0f, -4.0f));
		Pose.GripLocal[BackHand] = BoardGrabPoints::ToBoardLocal(BoardGrabPoints::ToeRailBack, N);
		Pose.HandOnBoard[BackHand] = 1.0f;
		Pose.TorsoPitchDeg = 15.0f;
		// The legs stretched out down and back behind the board.
		SetAnkles(FVector(-35.0f, -RiderRig::HipHalfWidthCm, -76.0f));
		break;
	}
	case ETrickBoardOff::TicTac:
	{
		// On the back hand's side, nose forwards and the toe edge on top, in the back hand; then turned
		// TicTacDeg about the board's long axis through the hand, so the same rail ends in it.
		const FQuat Rotation = FRotationMatrix::MakeFromXY(Front, -Up * N).ToQuat();
		const FVector Grip = BoardGrabPoints::SocketFor(ETrickGrabZone::ToeEdge, ETrickHand::Back, N);
		const FVector GripInRider = FVector(35.0f, 0.0f, 20.0f) + BackHandSide * 40.0f;
		const FTransform Hold(Rotation, GripInRider - Rotation.RotateVector(Grip));
		const FQuat Spin(FVector::ForwardVector, FMath::DegreesToRadians(TicTacDeg));
		Pose.BoardInRider = FTransform(Spin, Grip - Spin.RotateVector(Grip)) * Hold;
		Pose.GripLocal[BackHand] = Grip;
		Pose.HandOnBoard[BackHand] = 1.0f;
		Pose.TorsoPitchDeg = 10.0f;
		break;
	}
	case ETrickBoardOff::BoardPass:
	{
		// Stood on its tail with the deck to the rider, carried round the waist by the handle.
		const float Phase = FMath::Clamp(PassPhase, 0.0f, 1.0f);
		const float Angle = 2.0f * PI * Phase;
		const FVector Out = Front * FMath::Cos(Angle) + BackHandSide * FMath::Sin(Angle);
		const FQuat Rotation = FRotationMatrix::MakeFromXZ(Up, -Out).ToQuat();
		Pose.BoardInRider = FTransform(Rotation, Up * 30.0f + Out * 30.0f);
		const FVector Handle = BoardGrabPoints::ToBoardLocal(BoardGrabPoints::Handle, N);
		Pose.GripLocal[FrontHand] = Handle + FVector(6.0f, 0.0f, 0.0f);
		Pose.GripLocal[BackHand] = Handle - FVector(6.0f, 0.0f, 0.0f);
		// The back hand carries it to behind the back, the front hand takes it there: both are on it only
		// close to the middle of the back, where both reach.
		Pose.HandOnBoard[BackHand] = 1.0f - FMath::SmoothStep(0.5f, 0.54f, Phase);
		Pose.HandOnBoard[FrontHand] = FMath::SmoothStep(0.46f, 0.5f, Phase);
		const float Behind = FMath::Clamp(-static_cast<float>(FVector::DotProduct(Out, Front)), 0.0f, 1.0f);
		Pose.HandBehind[FrontHand] = Behind;
		Pose.HandBehind[BackHand] = Behind;
		// Legs nearly straight, so the board clears the knees in front.
		SetAnkles(FVector(-5.0f, -RiderRig::HipHalfWidthCm, -78.0f));
		break;
	}
	case ETrickBoardOff::Plain:
	default:
	{
		// In front of the hips, deck up and tilted towards the chest, both hands on the toe rail.
		const FQuat Rotation = FRotationMatrix::MakeFromXZ(NoseSide, (Up - Front * 0.7f).GetSafeNormal()).ToQuat();
		Pose.BoardInRider = FTransform(Rotation, FVector(32.0f, 0.0f, 0.0f));
		Pose.GripLocal[FrontHand] = BoardGrabPoints::ToBoardLocal(BoardGrabPoints::ToeRailFront, N);
		Pose.GripLocal[BackHand] = BoardGrabPoints::ToBoardLocal(BoardGrabPoints::ToeRailBack, N);
		Pose.HandOnBoard[FrontHand] = 1.0f;
		Pose.HandOnBoard[BackHand] = 1.0f;
		Pose.TorsoPitchDeg = 20.0f;
		break;
	}
	}
	return Pose;
}
