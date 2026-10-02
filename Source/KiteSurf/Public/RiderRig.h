#pragma once

#include "CoreMinimal.h"

/** One two-bone limb: where it starts, where it bends, where it ends, and which way the bend points. */
struct FRiderLimbPose
{
	FVector Root = FVector::ZeroVector;
	FVector Joint = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	FVector Pole = FVector::UpVector;
};

/** What the rider's body is asked to do. */
struct FRiderRigInput
{
	/** The board: the feet are in its straps, so they go where it goes, tilt and all. */
	FTransform Board = FTransform::Identity;
	/** The way the body faces, level (unit). */
	FVector Facing = FVector::ForwardVector;
	/** The line from the feet up through the body: vertical when standing straight, tilted when leaning (unit). */
	FVector BodyUp = FVector::UpVector;
	/** 0 standing, 1 a full crouch. */
	float Crouch = 0.0f;
};

/** The whole figure, posed. Index 0 is the rider's left, 1 their right. */
struct FRiderRigPose
{
	FVector Pelvis = FVector::ZeroVector;
	FQuat Torso = FQuat::Identity;
	FRiderLimbPose Legs[2];
	FRiderLimbPose Arms[2];
};

/**
 * Poses the jointed rider: the feet stay in the straps and the hands go to the bar, and the knees
 * and elbows bend to make that fit. The lengths and joint positions are those of the meshes built
 * by scripts/editor/generate_mesh_objs.py (RIDER_* and build_rider_torso): change them together.
 */
namespace RiderRig
{
	constexpr float ThighLengthCm = 45.0f;
	constexpr float ShinLengthCm = 42.0f;
	constexpr float UpperArmLengthCm = 26.5f;
	constexpr float ForearmLengthCm = 27.0f;

	/** The hips either side of the pelvis, and the shoulders above it, in the torso's own axes (X forward, Y right, Z up). */
	constexpr float HipHalfWidthCm = 13.0f;
	const FVector ShoulderOffsetCm(4.0f, 23.0f, 52.0f);

	/** Pelvis height above the board standing with slightly bent knees, and how much of it a full crouch takes off. */
	constexpr float StandingPelvisHeightCm = 80.0f;
	constexpr float CrouchDropFraction = 0.4f;

	/** The straps: either side of the middle of the board along its length, and how high the ankle sits above the deck. */
	constexpr float StrapHalfSpacingCm = 30.0f;
	constexpr float AnkleHeightCm = 7.0f;

	/**
	 * Where a two-bone limb bends. The end is put at Target, or as near as the limb reaches; the
	 * bend points as close to Pole as the limb's line allows. Both bone lengths are kept exactly.
	 */
	KITESURF_API FVector SolveTwoBone(const FVector& Root, const FVector& Target, const FVector& Pole, float UpperLength, float LowerLength, FVector& OutEnd);

	/** Pelvis, torso and legs. The arms are left hanging towards where the hands would hold a bar. */
	KITESURF_API FRiderRigPose SolveBody(const FRiderRigInput& Input);

	/** Puts the hands on these points (or as near as the arms reach). */
	KITESURF_API void SolveArms(FRiderRigPose& Pose, const FVector& LeftHand, const FVector& RightHand);

	/** Where to draw a limb part: at Start, with its bone (+X) along the line to End and its bend side (+Z) towards Pole. */
	KITESURF_API FTransform SegmentTransform(const FVector& Start, const FVector& End, const FVector& Pole);
}
