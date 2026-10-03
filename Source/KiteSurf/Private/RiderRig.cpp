#include "RiderRig.h"

FVector RiderRig::SolveTwoBone(const FVector& Root, const FVector& Target, const FVector& Pole, float UpperLength, float LowerLength, FVector& OutEnd)
{
	FVector ToTarget = Target - Root;
	float Distance = ToTarget.Size();
	const FVector Direction = Distance > KINDA_SMALL_NUMBER ? ToTarget / Distance : FVector::DownVector;

	// Neither quite straight nor folded flat: a straight limb has no bend to point anywhere.
	const float Longest = (UpperLength + LowerLength) * 0.995f;
	const float Shortest = FMath::Abs(UpperLength - LowerLength) + 1.0f;
	Distance = FMath::Clamp(Distance, Shortest, Longest);
	OutEnd = Root + Direction * Distance;

	// The bend sits on a circle round the limb's line; the pole picks the point on it.
	const float Along = (FMath::Square(UpperLength) - FMath::Square(LowerLength) + FMath::Square(Distance)) / (2.0f * Distance);
	const float Out = FMath::Sqrt(FMath::Max(FMath::Square(UpperLength) - FMath::Square(Along), 0.0f));
	FVector Bend = Pole - FVector::DotProduct(Pole, Direction) * Direction;
	if (!Bend.Normalize())
	{
		// The pole lies along the limb: any side will do, so take a steady one.
		Bend = FVector::CrossProduct(Direction, FVector::RightVector);
		if (!Bend.Normalize())
		{
			Bend = FVector::CrossProduct(Direction, FVector::ForwardVector).GetSafeNormal();
		}
	}
	return Root + Direction * Along + Bend * Out;
}

FTransform RiderRig::SegmentTransform(const FVector& Start, const FVector& End, const FVector& Pole)
{
	const FVector Along = (End - Start).GetSafeNormal();
	if (Along.IsNearlyZero())
	{
		return FTransform(FQuat::Identity, Start);
	}
	return FTransform(FRotationMatrix::MakeFromXZ(Along, Pole).ToQuat(), Start);
}

FQuat RiderRig::MakeBodyQuat(const FVector& Facing, const FVector& BodyUp)
{
	const FVector LevelFacing = Facing.GetSafeNormal2D().IsNearlyZero() ? FVector::ForwardVector : Facing.GetSafeNormal2D();
	const FVector Up = BodyUp.GetSafeNormal().IsNearlyZero() ? FVector::UpVector : BodyUp.GetSafeNormal();
	return FRotationMatrix::MakeFromXZ(LevelFacing, Up).ToQuat();
}

FRiderRigPose RiderRig::SolveBody(const FRiderRigInput& Input)
{
	FRiderRigPose Pose;

	// The body's frame. Facing is where the knees point, Right the side the right foot is on and
	// the knees spread towards, BodyUp the line the pelvis sits on above the feet.
	FVector Facing;
	FVector BodyUp;
	FVector Right;
	if (Input.BodyQuat.IsSet())
	{
		// Any orientation: everything comes from the body itself, so upside down the pelvis is
		// still above the feet in the body's terms and the knees still bend towards the chest.
		Pose.Torso = Input.BodyQuat.GetValue().GetNormalized();
		Facing = Pose.Torso.GetAxisX();
		Right = Pose.Torso.GetAxisY();
		BodyUp = Pose.Torso.GetAxisZ();
	}
	else
	{
		// Standing on the water: the facing is level and the knees point along it.
		Facing = Input.Facing.GetSafeNormal2D().IsNearlyZero() ? FVector::ForwardVector : Input.Facing.GetSafeNormal2D();
		BodyUp = Input.BodyUp.GetSafeNormal().IsNearlyZero() ? FVector::UpVector : Input.BodyUp.GetSafeNormal();
		Right = FVector::CrossProduct(FVector::UpVector, Facing);
		Pose.Torso = FRotationMatrix::MakeFromXZ(Facing, BodyUp).ToQuat();
	}
	Pose.Hips = Pose.Torso;
	if (Input.PelvisUp.IsSet() && !Input.PelvisUp.GetValue().GetSafeNormal().IsNearlyZero())
	{
		// A hand-over between the two: the pelvis line is blended separately from the torso.
		BodyUp = Input.PelvisUp.GetValue().GetSafeNormal();
	}

	// The feet are in the straps, one each side of the middle of the board along its length. The
	// rider stands across the board, so which strap is under their right foot depends on which
	// rail they face.
	const FVector BoardCentre = Input.Board.GetLocation();
	const FVector BoardAlong = Input.Board.GetUnitAxis(EAxis::X);
	const FVector BoardUp = Input.Board.GetUnitAxis(EAxis::Z);
	const float RightStrapSign = FVector::DotProduct(BoardAlong, Right) >= 0.0f ? 1.0f : -1.0f;
	FVector Ankles[2];
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const float SideSign = Side == 0 ? -1.0f : 1.0f;
		Ankles[Side] = BoardCentre + BoardAlong * (StrapHalfSpacingCm * SideSign * RightStrapSign) + BoardUp * AnkleHeightCm;
		// A foot out of its strap goes where it is told instead.
		if (Input.Feet[Side].AnkleTarget.IsSet())
		{
			Ankles[Side] = Input.Feet[Side].AnkleTarget.GetValue();
		}
	}

	// The pelvis is over the feet along the body's line, lower in a crouch, and never further
	// from a strap than the leg reaches: a rider leaning right out sits lower.
	const float LegReach = (ThighLengthCm + ShinLengthCm) * 0.985f;
	float Height = StandingPelvisHeightCm * (1.0f - CrouchDropFraction * FMath::Clamp(Input.Crouch, 0.0f, 1.0f));
	for (int32 Try = 0; Try < 12 && !Input.PelvisAnchor.IsSet(); ++Try)
	{
		Pose.Pelvis = BoardCentre + BoardUp * AnkleHeightCm + BodyUp * Height;
		bool bReaches = true;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const float SideSign = Side == 0 ? -1.0f : 1.0f;
			const FVector Hip = Pose.Pelvis + Pose.Hips.RotateVector(FVector(0.0f, HipHalfWidthCm * SideSign, 0.0f));
			bReaches = bReaches && FVector::Dist(Hip, Ankles[Side]) <= LegReach;
		}
		if (bReaches)
		{
			break;
		}
		Height *= 0.94f;
	}
	if (Input.PelvisAnchor.IsSet())
	{
		// The body holds still and the board comes to it (a grab in the air).
		Pose.Pelvis = Input.PelvisAnchor.GetValue();
	}

	for (int32 Side = 0; Side < 2; ++Side)
	{
		const float SideSign = Side == 0 ? -1.0f : 1.0f;
		FRiderLimbPose& Leg = Pose.Legs[Side];
		Leg.Root = Pose.Pelvis + Pose.Hips.RotateVector(FVector(0.0f, HipHalfWidthCm * SideSign, 0.0f));
		// Knees forwards and a little apart.
		Leg.Pole = Facing + Right * (0.35f * SideSign);
		Leg.Joint = SolveTwoBone(Leg.Root, Ankles[Side], Leg.Pole, ThighLengthCm, ShinLengthCm, Leg.End);
	}

	// The twist over the hips (riding toeside): about the body's Up, so the legs are untouched and the
	// shoulders, the arms and the drawn torso turn.
	if (Input.TorsoTwistDeg != 0.0f)
	{
		Pose.Torso = (Pose.Torso * FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(Input.TorsoTwistDeg))).GetNormalized();
	}

	// The fold at the hips: about the torso's own side axis, which the hips lie on, so the legs are
	// untouched and only the shoulders (and so the arms) and the drawn torso come forwards.
	if (Input.TorsoPitchDeg != 0.0f)
	{
		Pose.Torso = (Pose.Torso * FQuat(FVector::YAxisVector, FMath::DegreesToRadians(Input.TorsoPitchDeg))).GetNormalized();
	}

	// Until told where the bar is, the hands are held out in front at waist height.
	SolveArms(Pose,
		Pose.Pelvis + Pose.Torso.RotateVector(FVector(46.0f, -22.0f, 22.0f)),
		Pose.Pelvis + Pose.Torso.RotateVector(FVector(46.0f, 22.0f, 22.0f)));
	return Pose;
}

void RiderRig::SolveArms(FRiderRigPose& Pose, const FVector& LeftHand, const FVector& RightHand)
{
	// Elbows down and out.
	SolveArm(Pose, 0, LeftHand, DefaultElbowPole(Pose, 0));
	SolveArm(Pose, 1, RightHand, DefaultElbowPole(Pose, 1));
}

FVector RiderRig::ShoulderPosition(const FRiderRigPose& Pose, int32 Side)
{
	const float SideSign = Side == 0 ? -1.0f : 1.0f;
	return Pose.Pelvis + Pose.Torso.RotateVector(FVector(ShoulderOffsetCm.X, ShoulderOffsetCm.Y * SideSign, ShoulderOffsetCm.Z));
}

void RiderRig::SolveArm(FRiderRigPose& Pose, int32 Side, const FVector& Hand, const FVector& ElbowPole)
{
	FRiderLimbPose& Arm = Pose.Arms[Side == 0 ? 0 : 1];
	Arm.Root = ShoulderPosition(Pose, Side);
	Arm.Pole = ElbowPole;
	// SolveTwoBone keeps both bones their length and, out of reach, stops the hand on the line to Hand.
	Arm.Joint = SolveTwoBone(Arm.Root, Hand, Arm.Pole, UpperArmLengthCm, ForearmLengthCm, Arm.End);
}

FVector RiderRig::DefaultElbowPole(const FRiderRigPose& Pose, int32 Side)
{
	const float SideSign = Side == 0 ? -1.0f : 1.0f;
	return -Pose.Torso.GetAxisZ() + Pose.Torso.GetAxisY() * (0.6f * SideSign);
}

FVector RiderRig::GrabElbowPole(const FRiderRigPose& Pose, int32 Side)
{
	const float SideSign = Side == 0 ? -1.0f : 1.0f;
	return Pose.Torso.GetAxisY() * SideSign + Pose.Torso.GetAxisX() * 0.3f;
}

FVector RiderRig::BehindBackElbowPole(const FRiderRigPose& Pose, int32 Side)
{
	const float SideSign = Side == 0 ? -1.0f : 1.0f;
	return Pose.Torso.GetAxisY() * SideSign - Pose.Torso.GetAxisX() * 0.5f;
}

FVector RiderRig::BehindBackHand(const FRiderRigPose& Pose, int32 Side)
{
	const float SideSign = Side == 0 ? -1.0f : 1.0f;
	return Pose.Pelvis + Pose.Torso.RotateVector(FVector(-22.0f, 6.0f * SideSign, 12.0f));
}

FVector RiderRig::HandTarget(const FRiderRigPose& Pose, const FRiderRigInput& Input, int32 Side, const FVector& BarHand)
{
	const FRiderHandInput& Hand = Input.Hands[Side == 0 ? 0 : 1];
	switch (Hand.Target)
	{
	case ERiderHandTarget::BoardSocket:
		return (Hand.SocketBoard.IsSet() ? Hand.SocketBoard.GetValue() : Input.Board).TransformPosition(Hand.BoardSocket);
	case ERiderHandTarget::Free:
		return Hand.WorldTarget;
	case ERiderHandTarget::BehindBack:
		return BehindBackHand(Pose, Side);
	case ERiderHandTarget::Bar:
	default:
		return BarHand;
	}
}

FVector RiderRig::HandElbowPole(const FRiderRigPose& Pose, const FRiderRigInput& Input, int32 Side)
{
	const FRiderHandInput& Hand = Input.Hands[Side == 0 ? 0 : 1];
	if (Hand.ElbowPole.IsSet())
	{
		return Hand.ElbowPole.GetValue();
	}
	switch (Hand.Target)
	{
	case ERiderHandTarget::BoardSocket:
		return GrabElbowPole(Pose, Side);
	case ERiderHandTarget::BehindBack:
		return BehindBackElbowPole(Pose, Side);
	case ERiderHandTarget::Bar:
	case ERiderHandTarget::Free:
	default:
		return DefaultElbowPole(Pose, Side);
	}
}

void RiderRig::SolveArmsPerHand(FRiderRigPose& Pose, const FRiderRigInput& Input, const FVector& BarLeft, const FVector& BarRight)
{
	for (int32 Side = 0; Side < 2; ++Side)
	{
		SolveArm(Pose, Side, HandTarget(Pose, Input, Side, Side == 0 ? BarLeft : BarRight), HandElbowPole(Pose, Input, Side));
	}
}
