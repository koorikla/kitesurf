#pragma once

#include "CoreMinimal.h"
#include "Tricks/TrickTypes.h"

/**
 * The frame a jump's rotations are measured in, frozen at take-off (docs/tricks.md 6.6, RiderAxes):
 * U = world up, T = horizontal travel direction, S = U x T, and Sigma, the side the rider travels
 * towards (RiderAxes::TravelSide), which tells a back roll from a front roll.
 */
struct KITESURF_API FRotationTakeoffFrame
{
	FVector U = FVector::UpVector;
	FVector T = FVector::ForwardVector;
	FVector S = FVector::RightVector;
	float Sigma = 1.0f;

	/**
	 * The frame for a rider with this body orientation, board velocity (any units) and board nose.
	 * T is the horizontal velocity; with no horizontal velocity, the board's nose; failing that, the
	 * body's front. Sigma is RiderAxes::TravelSide(Body, Velocity, BoardForward).
	 */
	static FRotationTakeoffFrame Make(const FQuat& Body, const FVector& Velocity, const FVector& BoardForward);
};

/** One counted inversion. Times are seconds since Begin. */
struct FRecognizedInversion
{
	ETrickInversion Kind = ETrickInversion::BackRoll;

	/** When the rotation that carried it started: the start of the run of steps turning at RollStartRateDegS or faster that led into it. */
	float StartSeconds = 0.0f;

	/** When body Up went below CountDot. */
	float CountedSeconds = 0.0f;
};

/** What FRotationRecognizer made of a jump's rotation. */
struct KITESURF_API FRotationResult
{
	/** Inversions in the order they were counted. */
	TArray<FRecognizedInversion> Inversions;

	/** Body spin credited, in half turns (2 is a 360). */
	int32 SpinHalfTurns = 0;

	/** Backside is the back roll's sense (about -Sigma * U); None when no spin is credited. */
	ETrickSense SpinSense = ETrickSense::None;

	/**
	 * The rotation about U the spin is credited from (deg, signed, right-handed about U): the
	 * integral of omega . U, less the flight's own turn when velocities were given.
	 */
	float SpinDeg = 0.0f;

	/**
	 * Heelside, or Blind or Toeside when the rider lands facing away: the net heading more than
	 * StanceHeadingDeg off, so the chest is on the other side of the travel from the take-off's
	 * (the kite's side). Heelside while still in the air.
	 */
	ETrickStance LandingStance = ETrickStance::Heelside;

	/** Net heading at touchdown (deg, -180..180): the twist about U of qLand * qTakeoff^-1, less the flight's turn. 0 in the air. */
	float NetHeadingDeg = 0.0f;

	/** Tilt of the body at touchdown against its take-off attitude (deg): the swing of qLand * qTakeoff^-1. 0 in the air. */
	float LandingTiltDeg = 0.0f;

	/** The flight's own heading turn over the jump (deg, signed about U), from the velocities given to Step; 0 when none were. */
	float FlightTurnDeg = 0.0f;

	/** The first inversion's StartSeconds; negative when nothing inverted. Feeds the loops' early or late roll (TrickRecognition::LoopRollTiming). */
	float RollStartSeconds = -1.0f;

	/** The inversion kinds alone, in order. */
	TArray<ETrickInversion> InversionKinds() const;
};

/** Thresholds of FRotationRecognizer. The arm and count dots are docs/tricks.md 6.6's; the rest are estimates. */
struct FRotationRecognizerSettings
{
	/** dot(body Up, U) above this arms the next inversion. */
	float ArmDot = 0.3f;

	/** dot(body Up, U) below this, armed, counts an inversion. */
	float CountDot = -0.3f;

	/** A rotation counts as started once the body turns at least this fast (deg/s), for the roll start time. Estimate. */
	float RollStartRateDegS = 90.0f;

	/** Tilt of the reference back roll axis from Up (deg): an inversion turning along RiderAxes::BackRollAxisBody(Sigma, this) is a back roll. */
	float ReferenceRollTiltDeg = 65.0f;

	/** A net heading further than this from the take-off's (deg) lands blind or toeside. */
	float StanceHeadingDeg = 90.0f;

	/**
	 * Below this length of the twist part (|(w, v . U)|), the rotation is within a few degrees of a
	 * half turn about a horizontal axis and the twist is ill-conditioned: the net heading comes from
	 * the body's front projected on the water instead.
	 */
	float TwistGuard = 0.05f;
};

/**
 * Counts a jump's rotations from the rider's attitude (T1.6, docs/tricks.md 6.6). Pure: no UObject,
 * no world. FJumpRecorder runs one per jump from the attitude fields of FJumpRecorderInput.
 *
 * - Begin: the take-off frame and body orientation.
 * - Step, once per fixed step in the air, with the body orientation at the end of the step and
 *   the angular velocity (rad/s, world):
 *   - omega is integrated on the frozen take-off axes U, T, S and on the body axes;
 *   - an inversion is counted when dot(body Up, U) goes below CountDot after being above ArmDot
 *     (the take-off arms it only when the body starts above ArmDot);
 *   - it is a flip when the body-frame rotation since the arming is mostly about Right (a
 *     backflip about -Right), otherwise a roll, back when that rotation runs along
 *     RiderAxes::BackRollAxisBody(Sigma, ReferenceRollTiltDeg), front otherwise.
 * - Finish, with the body at touchdown:
 *   - with no inversion, the spin is the integral of omega . U less the flight's turn, snapped to
 *     half turns as floor((|spin| + 45) / 180) (SnapHalfTurns). When that count's parity
 *     disagrees with the landing (odd lands facing away, even facing the kite), it moves one half
 *     turn towards the integral: a 120 deg turn that lands blind is a 180, a 150 deg one that
 *     lands facing the kite is none;
 *   - the rider lands facing away when the net heading is more than StanceHeadingDeg off: the
 *     swing-twist of qLand * qTakeoff^-1 about U, less the flight's turn, guarded near a half turn
 *     about a horizontal axis (TwistGuard). Heelside at take-off is the chest on the kite's side of
 *     the travel, so facing away is the chest on the other side. It lands Blind after a backside
 *     turn, Toeside after a frontside one;
 *   - with inversions, the integral holds each roll's own turn about U (cos(tilt) of it), so the
 *     spin is the landing's instead: one half turn when the rider lands facing away, none
 *     otherwise (back to blind is a back roll plus a backside 180);
 *   - the spin sense, and blind or toeside, follow the first roll when there is one (a back roll
 *     is backside by definition, so back to blind); otherwise backside when the integral turned
 *     against Sigma about U.
 */
class KITESURF_API FRotationRecognizer
{
public:
	FRotationRecognizer() = default;
	explicit FRotationRecognizer(const FRotationRecognizerSettings& InSettings) : Settings(InSettings) {}

	/** Starts a jump. */
	void Begin(const FRotationTakeoffFrame& Frame, const FQuat& Body);

	/**
	 * One step of Dt seconds: Body at the end of the step, OmegaW in rad/s (world). VelocityCmS (any
	 * units), when not zero along the water, follows the flight's heading for the spin and stance.
	 */
	void Step(const FQuat& Body, const FVector& OmegaW, float Dt, const FVector& Velocity = FVector::ZeroVector);

	/** The result at touchdown with the body orientation there. */
	FRotationResult Finish(const FQuat& LandingBody) const;

	/**
	 * The result so far, for the live ticker: the inversions counted, and the spin credited so far
	 * (snapped, with no landing to settle the parity; with inversions, a half turn only while
	 * upright and more than StanceHeadingDeg off). The landing fields are left at their defaults.
	 */
	FRotationResult GetCurrent() const;

	bool HasBegun() const { return bBegun; }

	/** Steps since Begin. */
	int32 GetStepCount() const { return StepCount; }

	/** Integral of omega . U, T, S since Begin (rad). */
	double GetSpinAboutURad() const { return AboutURad; }
	double GetAboutTRad() const { return AboutTRad; }
	double GetAboutSRad() const { return AboutSRad; }

	/** Integral of the body-frame angular velocity since Begin (rad; Front, Right, Up). */
	FVector GetBodyAxisRad() const { return BodyAxisRad; }

	const FRotationTakeoffFrame& GetFrame() const { return Frame; }

	/** floor((|SpinDeg| + 45) / 180): 135 deg is the first half turn, 315 the second. */
	static int32 SnapHalfTurns(float SpinDeg);

	/**
	 * Splits Q into Swing * Twist, Twist about Axis (unit). When the twist part is shorter than
	 * 1e-6 (a half turn about an axis at right angles to Axis), Twist is identity.
	 */
	static void SwingTwist(const FQuat& Q, const FVector& Axis, FQuat& OutSwing, FQuat& OutTwist);

	FRotationRecognizerSettings Settings;

private:
	ETrickSense SenseOf(double SpinRad) const;
	/** The sense of the spin credited and of a landing facing away: the first roll's (back roll backside), else the integral's. */
	ETrickSense LandingSense() const;
	float NetHeadingDeg(const FQuat& Body, float* OutTiltDeg) const;
	void FillSpin(FRotationResult& Result, bool bFacingAway, bool bLanded) const;

	bool bBegun = false;
	FRotationTakeoffFrame Frame;
	FQuat Body0 = FQuat::Identity;
	FQuat LastBody = FQuat::Identity;
	double AboutURad = 0.0;
	double AboutTRad = 0.0;
	double AboutSRad = 0.0;
	FVector BodyAxisRad = FVector::ZeroVector;
	FVector BodyAxisSinceArm = FVector::ZeroVector;
	bool bArmed = false;
	float TimeSeconds = 0.0f;
	/** When the current run of fast rotation began (s since Begin); negative while slower than RollStartRateDegS. */
	float RotatingSinceSeconds = -1.0f;
	/** The flight's heading turn so far (rad, unwrapped) and the last travel direction seen. */
	double FlightTurnRad = 0.0;
	FVector LastTravel = FVector::ZeroVector;
	int32 StepCount = 0;
	TArray<FRecognizedInversion> Inversions;
};
