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

/** Where a hand goes. */
enum class ERiderHandTarget : uint8
{
	/** On the bar: the hand point SolveArmsPerHand is given for this side, elbow down and out. */
	Bar,
	/** On the board: FRiderHandInput::BoardSocket on SocketBoard (or the strapped board), elbow out to the side. */
	BoardSocket,
	/** Anywhere: FRiderHandInput::WorldTarget, elbow down and out unless told otherwise. */
	Free,
	/** Behind the back at the hip (RiderRig::BehindBackHand), elbow out and back. */
	BehindBack
};

/**
 * What one hand is asked to do. By default it is on the bar, exactly as SolveArms puts it. A target
 * the arm cannot reach is clamped to the arm's reach along the line from the shoulder towards it.
 */
struct FRiderHandInput
{
	ERiderHandTarget Target = ERiderHandTarget::Bar;
	/** Free: where the hand goes (world, cm). */
	FVector WorldTarget = FVector::ZeroVector;
	/** BoardSocket: the grip point on the board (board-local, cm), for example from BoardGrabPoints::SocketFor. */
	FVector BoardSocket = FVector::ZeroVector;
	/** BoardSocket: the board the socket is on (world). Unset, the strapped board, FRiderRigInput::Board. */
	TOptional<FTransform> SocketBoard;
	/** Which way the elbow points (world direction). Unset, the target's default pole (see ERiderHandTarget). */
	TOptional<FVector> ElbowPole;
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
	/**
	 * The line the pelvis sits on above the feet, when it is not the body's own Up (unit). Only for
	 * handing over between the level solve and BodyQuat: the level solve puts the pelvis along the
	 * tilted BodyUp while its torso stays square to the level Facing, which no single quaternion
	 * reproduces, so the pawn blends this from BodyUp to BodyQuat's Up while it blends the torso.
	 * Unset, the pelvis goes along BodyQuat's Up (or BodyUp, without BodyQuat).
	 */
	TOptional<FVector> PelvisUp;
	/** 0 standing, 1 a full crouch. */
	float Crouch = 0.0f;
	/**
	 * Where the pelvis is (world, cm), instead of over the feet at the crouch's height. For a grab
	 * (T2.1) in the air: the body stays where it is and the board is pulled up towards it, so the
	 * knees come up. The legs still reach the straps as long as the board is within the legs' reach
	 * of the hips. Unset, the pelvis sits over the feet as always.
	 */
	TOptional<FVector> PelvisAnchor;
	/**
	 * The torso folded forwards at the hips (deg; + brings the chest down towards Front), for a grab
	 * (T2.1). The fold is about the hip axis, so the hips, the pelvis and the legs do not move; the
	 * shoulders, the arms and the drawn torso follow it. 0 leaves the torso along the body.
	 */
	float TorsoPitchDeg = 0.0f;
	/**
	 * The torso twisted over the hips about the body's Up (deg; + turns the chest towards Right), for
	 * riding toeside (T3.5): the hips and the legs stay on the body's frame (FRiderRigPose::Hips), the
	 * torso, the shoulders and so the arms turn. Applied before TorsoPitchDeg. 0 leaves the torso on the hips.
	 */
	float TorsoTwistDeg = 0.0f;
	/** Index 0 is the rider's left foot, 1 their right. */
	FRiderFootInput Feet[2];
	/**
	 * Index 0 is the rider's left hand, 1 their right. Read by SolveArmsPerHand only: SolveBody and
	 * SolveArms ignore it. Both on the bar (the default) gives exactly what SolveArms gives.
	 */
	FRiderHandInput Hands[2];
};

/** The whole figure, posed. Index 0 is the rider's left, 1 their right. */
struct FRiderRigPose
{
	FVector Pelvis = FVector::ZeroVector;
	/** The torso: the hips' frame with FRiderRigInput::TorsoTwistDeg and TorsoPitchDeg on it. The shoulders and arms hang off it. */
	FQuat Torso = FQuat::Identity;
	/** The pelvis's own frame (X Front, Y Right, Z Up), which the legs hang off: the torso before its twist and fold. */
	FQuat Hips = FQuat::Identity;
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

	/**
	 * The straps: either side of the middle of the board along its length, and how high the ankle
	 * sits above the deck. This is the one strap spacing: Tricks/BoardGrabPoints.h takes it from
	 * here, and generate_mesh_objs.py (STRAP_HALF_SPACING_CM) puts the board mesh's strap loops there.
	 */
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

	/** Pelvis, torso and legs (PelvisAnchor, TorsoTwistDeg and TorsoPitchDeg included). The arms are left hanging towards where the hands would hold a bar. */
	KITESURF_API FRiderRigPose SolveBody(const FRiderRigInput& Input);

	/** Puts the hands on these points (or as near as the arms reach). */
	KITESURF_API void SolveArms(FRiderRigPose& Pose, const FVector& LeftHand, const FVector& RightHand);

	/** Where the arm on this side (0 left, 1 right) starts: the shoulder, from the pose's pelvis and torso. */
	KITESURF_API FVector ShoulderPosition(const FRiderRigPose& Pose, int32 Side);

	/**
	 * Solves one arm (0 left, 1 right) to put its hand on Hand, or as near as it reaches along the
	 * line from the shoulder, with the elbow towards ElbowPole. The other arm is left as it is.
	 */
	KITESURF_API void SolveArm(FRiderRigPose& Pose, int32 Side, const FVector& Hand, const FVector& ElbowPole);

	/** The bar's elbow pole: down and out (-Up + 0.6 Right on the right, mirrored on the left), what SolveArms uses. */
	KITESURF_API FVector DefaultElbowPole(const FRiderRigPose& Pose, int32 Side);
	/** Reaching for the board: the elbow out to the side and a little forwards (Right + 0.3 Front, mirrored). */
	KITESURF_API FVector GrabElbowPole(const FRiderRigPose& Pose, int32 Side);
	/** A hand behind the back: the elbow out and back (Right - 0.5 Front, mirrored). */
	KITESURF_API FVector BehindBackElbowPole(const FRiderRigPose& Pose, int32 Side);
	/** Where a hand behind the back goes: at the hip, 22 cm behind the pelvis, 6 cm to its side and 12 cm up. */
	KITESURF_API FVector BehindBackHand(const FRiderRigPose& Pose, int32 Side);

	/** Where FRiderRigInput::Hands[Side] asks that hand to go (world, cm); BarHand is its point on the bar. */
	KITESURF_API FVector HandTarget(const FRiderRigPose& Pose, const FRiderRigInput& Input, int32 Side, const FVector& BarHand);
	/** The elbow pole for FRiderRigInput::Hands[Side]: its ElbowPole if set, otherwise its target's default. */
	KITESURF_API FVector HandElbowPole(const FRiderRigPose& Pose, const FRiderRigInput& Input, int32 Side);

	/**
	 * Solves each arm to its own target from FRiderRigInput::Hands: the bar point given here, a
	 * socket on the board, a free point or behind the back. Out of reach, the hand stops at the
	 * arm's reach on the line from the shoulder to the target. Both hands on the bar is exactly
	 * SolveArms(Pose, BarLeft, BarRight).
	 */
	KITESURF_API void SolveArmsPerHand(FRiderRigPose& Pose, const FRiderRigInput& Input, const FVector& BarLeft, const FVector& BarRight);

	/** Where to draw a limb part: at Start, with its bone (+X) along the line to End and its bend side (+Z) towards Pole. */
	KITESURF_API FTransform SegmentTransform(const FVector& Start, const FVector& End, const FVector& Pole);
}
