#pragma once

#include "CoreMinimal.h"
#include "Tricks/TrickTypes.h"

/** Thresholds of FRaleyRecognizer (docs/tricks/T3.md T3.2). Estimates. */
struct FRaleyRecognizerSettings
{
	/** The body's Up has to tilt further than this from world up for a raley (deg). docs/tricks.md: over 60. */
	float RaleyMinTiltDeg = 60.0f;

	/**
	 * At the most tilted step, Up's lean on the water has to point within this of the kite's direction
	 * on the water (deg): the line swung the body out towards the kite. The plan's "swing axis within
	 * 40 deg of Side", with Side read as the line's side (square to the line on the water), since the
	 * kite usually sits towards the board's nose rather than square to the body.
	 */
	float SwingToKiteMaxDeg = 40.0f;

	/** A turn about the lines of at least this, with the raley's arms out and no pass, is an S-bend (deg). */
	float SBendMinLineSpinDeg = 270.0f;
};

/** What FRaleyRecognizer made of a jump. */
struct KITESURF_API FRaleyResult
{
	/** The line pull swung the body out past RaleyMinTiltDeg towards the kite, unhooked, the raley's arms out, with no inversion and no S-bend. */
	bool bRaley = false;

	/** A raley with a turn of SBendMinLineSpinDeg or more about the lines and no pass. */
	bool bSBend = false;

	/** Largest tilt of the body's Up from world up in the air (deg). */
	float MaxTiltDeg = 0.0f;

	/** At the most tilted step: the angle on the water between Up's lean and the kite's direction (deg, 0..180); 180 with no lean. */
	float SwingFromKiteDeg = 180.0f;

	/** The raley's arms were out as the tilt went past RaleyMinTiltDeg (a flip's arms in is no raley; pulling the bar in after that, to come back under, still is). */
	bool bArmsOutPastMinTilt = false;

	/** The turn about the lines while the raley's arms were out (deg, signed, + backside: about -Sigma x the line). */
	float LineSpinDeg = 0.0f;

	/** The S-bend's sense: backside is the S-bend, frontside the hinterberger. None when it is no S-bend. */
	ETrickSense SBendSense = ETrickSense::None;
};

/**
 * The raley and the S-bend from the rider's attitude and the lines (T3.2). Pure: no UObject. The jump
 * recorder runs one per jump next to FRotationRecognizer.
 *
 * - Begin with Sigma, the travel side (RiderAxes::TravelSide), which says which way is backside.
 * - Step once per fixed step in the air with the body at the end of the step, its angular velocity
 *   (rad/s, world), the unit direction to the kite (world) and whether the raley's arms are out
 *   (FAttitudeInputs::bRaleyArms). It keeps the largest tilt of Up from world up, the lean's angle to
 *   the kite on the water at that step, and, while the arms are out, the integral of omega . line.
 * - Get with the hook, the inversions and the passes the rest of the recognition found:
 *   - an S-bend is unhooked, |line turn| at least SBendMinLineSpinDeg, the tilt past RaleyMinTiltDeg
 *     and no pass; backside about -Sigma x the line (the back roll's sense) is the S-bend, frontside
 *     the hinterberger. Its overhead turn may take Up past the inversion count: the recorder drops
 *     those inversions, as the S-bend is that rotation;
 *   - otherwise a raley is unhooked, the tilt past RaleyMinTiltDeg, no inversion, the lean towards
 *     the kite within SwingToKiteMaxDeg, and the raley's arms out as the tilt went past
 *     RaleyMinTiltDeg (a flip pre-wind that comes up short, with the arms in, is not a raley; the bar
 *     pulled in after that to bring the board back under still is).
 */
class KITESURF_API FRaleyRecognizer
{
public:
	FRaleyRecognizer() = default;
	explicit FRaleyRecognizer(const FRaleyRecognizerSettings& InSettings) : Settings(InSettings) {}

	void Begin(float Sigma);

	void Step(const FQuat& Body, const FVector& OmegaW, const FVector& LineDirWorld, bool bRaleyArms, float Dt);

	FRaleyResult Get(bool bUnhooked, int32 InversionCount, int32 PassCount) const;

	bool HasBegun() const { return bBegun; }

	/** Tilt of the body's Up from world up (deg, 0..180). */
	static float TiltDeg(const FQuat& Body);

	/** Angle on the water between the body's lean (Up projected) and the line's direction (deg, 0..180); 180 when either has no horizontal part. */
	static float LeanFromLineDeg(const FQuat& Body, const FVector& LineDirWorld);

	FRaleyRecognizerSettings Settings;

private:
	bool bBegun = false;
	float Sigma = 1.0f;
	float MaxTiltDeg = 0.0f;
	float SwingFromKiteDeg = 180.0f;
	bool bArmsOutPastMinTilt = false;
	double LineSpinRad = 0.0;
};
