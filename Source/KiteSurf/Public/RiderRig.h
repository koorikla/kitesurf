#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"

/** One two-bone limb: where it starts, where it bends, where it ends, and which way the bend points. */
struct FRiderLimbPose
{
	FVector Root = FVector::ZeroVector;
	FVector Joint = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	FVector Pole = FVector::UpVector;
};

/**
 * What one foot is asked to do. By default it is in its strap. T2 adds its per-foot fields here
 * (a knee pole override, for example), so the input does not need reshuffling again.
 */
struct FRiderFootInput
{
	/** Where the ankle goes instead of its strap (world, cm): a one-footer or a board-off. Unset, the foot is in its strap. */
	TOptional<FVector> AnkleTarget;
};

/** What the rider's body is asked to do. */
struct FRiderRigInput
{
	/** The board: the feet are in its straps, so they go where it goes, tilt and all. */
	FTransform Board = FTransform::Identity;
	/** The way the body faces, level (unit). Ignored when BodyQuat is set. */
	FVector Facing = FVector::ForwardVector;
	/** The line from the feet up through the body: vertical when standing straight, tilted when leaning (unit). Ignored when BodyQuat is set. */
	FVector BodyUp = FVector::UpVector;
	/**
	 * The whole body's orientation, in any attitude, upside down included: X Front (the chest),
	 * Y Right, Z Up (feet to head), as in Tricks/RiderAxes.h. When set it replaces Facing and
	 * BodyUp: the torso, the line the pelvis sits on and the knee poles all come from it, so the
	 * knees bend forwards relative to the body however it is turned. Unset, the rig solves from the
	 * level Facing and BodyUp exactly as it always has. RiderRig::MakeBodyQuat builds the
	 * equivalent quaternion from those two.
	 */
	TOptional<FQuat> BodyQuat;
	/** 0 standing, 1 a full crouch. */
	float Crouch = 0.0f;
	/** Index 0 is the rider's left foot, 1 their right. */
	FRiderFootInput Feet[2];
	// Hands: SolveArms takes the hand points for now. T2.1 adds an FRiderHandInput Hands[2] here
	// (bar, board socket, free pose or behind the back), next to Feet.
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

	/**
	 * The torso the level solve draws for this Facing and BodyUp: X exactly along Facing made level,
	 * Z towards BodyUp (FRotationMatrix::MakeFromXZ, so the part of the lean along Facing is dropped).
	 * Passed as FRiderRigInput::BodyQuat with BodyUp vertical, it gives the level solve's pose. With a
	 * lean it does not: the level solve keeps that torso but puts the pelvis along the tilted BodyUp
	 * and points the knees along the level Facing, which no single body orientation reproduces.
	 */
	KITESURF_API FQuat MakeBodyQuat(const FVector& Facing, const FVector& BodyUp);

	/** Pelvis, torso and legs. The arms are left hanging towards where the hands would hold a bar. */
	KITESURF_API FRiderRigPose SolveBody(const FRiderRigInput& Input);

	/** Puts the hands on these points (or as near as the arms reach). */
	KITESURF_API void SolveArms(FRiderRigPose& Pose, const FVector& LeftHand, const FVector& RightHand);

	/** Where to draw a limb part: at Start, with its bone (+X) along the line to End and its bend side (+Z) towards Pole. */
	KITESURF_API FTransform SegmentTransform(const FVector& Start, const FVector& End, const FVector& Pole);
}
