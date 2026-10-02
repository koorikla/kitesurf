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

FRiderRigPose RiderRig::SolveBody(const FRiderRigInput& Input)
{
	FRiderRigPose Pose;

	const FVector Facing = Input.Facing.GetSafeNormal2D().IsNearlyZero() ? FVector::ForwardVector : Input.Facing.GetSafeNormal2D();
	const FVector BodyUp = Input.BodyUp.GetSafeNormal().IsNearlyZero() ? FVector::UpVector : Input.BodyUp.GetSafeNormal();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Facing);
	Pose.Torso = FRotationMatrix::MakeFromXZ(Facing, BodyUp).ToQuat();

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
	}

	// The pelvis is over the feet along the body's line, lower in a crouch, and never further
	// from a strap than the leg reaches: a rider leaning right out sits lower.
	const float LegReach = (ThighLengthCm + ShinLengthCm) * 0.985f;
	float Height = StandingPelvisHeightCm * (1.0f - CrouchDropFraction * FMath::Clamp(Input.Crouch, 0.0f, 1.0f));
	for (int32 Try = 0; Try < 12; ++Try)
	{
		Pose.Pelvis = BoardCentre + BoardUp * AnkleHeightCm + BodyUp * Height;
		bool bReaches = true;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const float SideSign = Side == 0 ? -1.0f : 1.0f;
			const FVector Hip = Pose.Pelvis + Pose.Torso.RotateVector(FVector(0.0f, HipHalfWidthCm * SideSign, 0.0f));
			bReaches = bReaches && FVector::Dist(Hip, Ankles[Side]) <= LegReach;
		}
		if (bReaches)
		{
			break;
		}
		Height *= 0.94f;
	}

	for (int32 Side = 0; Side < 2; ++Side)
	{
		const float SideSign = Side == 0 ? -1.0f : 1.0f;
		FRiderLimbPose& Leg = Pose.Legs[Side];
		Leg.Root = Pose.Pelvis + Pose.Torso.RotateVector(FVector(0.0f, HipHalfWidthCm * SideSign, 0.0f));
		// Knees forwards and a little apart.
		Leg.Pole = Facing + Right * (0.35f * SideSign);
		Leg.Joint = SolveTwoBone(Leg.Root, Ankles[Side], Leg.Pole, ThighLengthCm, ShinLengthCm, Leg.End);
	}

	// Until told where the bar is, the hands are held out in front at waist height.
	SolveArms(Pose,
		Pose.Pelvis + Pose.Torso.RotateVector(FVector(46.0f, -22.0f, 22.0f)),
		Pose.Pelvis + Pose.Torso.RotateVector(FVector(46.0f, 22.0f, 22.0f)));
	return Pose;
}

void RiderRig::SolveArms(FRiderRigPose& Pose, const FVector& LeftHand, const FVector& RightHand)
{
	const FVector BodyUp = Pose.Torso.GetAxisZ();
	const FVector Right = Pose.Torso.GetAxisY();
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const float SideSign = Side == 0 ? -1.0f : 1.0f;
		FRiderLimbPose& Arm = Pose.Arms[Side];
		Arm.Root = Pose.Pelvis + Pose.Torso.RotateVector(FVector(ShoulderOffsetCm.X, ShoulderOffsetCm.Y * SideSign, ShoulderOffsetCm.Z));
		// Elbows down and out.
		Arm.Pole = -BodyUp + Right * (0.6f * SideSign);
		Arm.Joint = SolveTwoBone(Arm.Root, Side == 0 ? LeftHand : RightHand, Arm.Pole, UpperArmLengthCm, ForearmLengthCm, Arm.End);
	}
}
