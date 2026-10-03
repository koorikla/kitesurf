#pragma once

#include "CoreMinimal.h"
#include "KiteMotionBar.generated.h"

/** How the motion bar reads the bar's power (sheet) from the controller. Steering is the roll either way. */
UENUM(BlueprintType)
enum class EMotionSheetMode : uint8
{
	/** Tip the pad towards you to pull the bar in (its pitch). */
	Tilt,
	/** Move the pad down or towards you to pull the bar in, up or away to let it out (its travel). */
	Move,
};

/**
 * One reading from a controller's motion sensors, in the controller's own axes as SDL reports
 * them for a pad held normally: +X to the right, +Y up through the face, +Z towards the player.
 */
struct FKiteMotionSample
{
	/** What the accelerometer feels, in g. At rest it is 1 g pointing up. */
	FVector AccelG = FVector::ZeroVector;
	/** How fast the pad is turning about each of its axes (rad/s, right-handed). */
	FVector GyroRadS = FVector::ZeroVector;
};

/** Somewhere motion readings come from: a real controller, or a scripted one in tests. */
class KITESURF_API IKiteMotionSource
{
public:
	virtual ~IKiteMotionSource() = default;

	/** The newest reading. False when there is no controller with motion sensors to read. */
	virtual bool Poll(FKiteMotionSample& OutSample) = 0;

	/** What is being read, for the settings screen ("DualSense Wireless Controller"); empty if nothing. */
	virtual FString GetDeviceName() const = 0;
};

/**
 * Works out how the controller is being held from its motion sensors: the gyro follows quick
 * movements and the accelerometer, which knows which way is down, takes out the gyro's drift.
 */
class KITESURF_API FMotionBarFilter
{
public:
	void Reset();

	void Update(const FKiteMotionSample& Sample, float DeltaTime);

	bool IsInitialized() const { return bInitialized; }

	/** Tilt to the right, as turning a bar to steer right: right hand down is positive (deg). */
	float GetRollDeg() const;

	/** Tilt of the pad as a bar pulled in towards the player: pulled in is positive (deg). */
	float GetPitchDeg() const;

	/** Which way is up, in the controller's axes (unit vector): what the accelerometer reads at rest. */
	const FVector& GetUp() const { return Up; }

	/** How long the accelerometer takes to pull the estimate back to true (s). */
	float AccelTimeConstantSeconds = 0.6f;

	/** The accelerometer is only trusted when it reads close to 1 g: this far either side (g). */
	float AccelTrustBandG = 0.25f;

private:
	/** Which way is up, in the controller's axes (Y is up through the pad's face). */
	FVector Up = FVector(0.0f, 1.0f, 0.0f);
	bool bInitialized = false;
};

/** Turns how the controller is held into the bar: tilt to steer, tip towards you to pull it in. */
struct KITESURF_API FMotionBarMapping
{
	/** Roll for full steering (deg). */
	float SteerFullDeg = 35.0f;
	/** Roll either side of level that does nothing (deg). */
	float SteerDeadzoneDeg = 3.0f;
	/** Pitch that takes the bar through its whole throw (deg). */
	float SheetRangeDeg = 50.0f;

	/** How the pad was held when the bar was last centred, and where the bar was then. */
	float NeutralRollDeg = 0.0f;
	float NeutralPitchDeg = 0.0f;
	float SheetAtNeutral = 0.5f;

	/** Takes the way the pad is held now as "bar level, at this position". */
	void Calibrate(float RollDeg, float PitchDeg, float CurrentSheet);

	/** -1..1 */
	float GetSteer(float RollDeg) const;
	/** 0..1 */
	float GetSheet(float PitchDeg) const;

	/**
	 * Hand travel that takes the bar through its whole throw in the Move mode (cm): pulled down or
	 * in this far from where it was centred, the bar goes from fully out to fully in. Estimate.
	 */
	float MoveSheetFullStrokeCm = 25.0f;

	/** 0..1: the bar for the pad moved this far (down or in positive, cm) since it was centred. */
	float GetSheetFromStroke(float StrokeCm) const;

	/** How far the stroke can go either way from the centre before the bar is at a stop (cm). */
	float GetStrokeMinCm() const;
	float GetStrokeMaxCm() const;
};

/**
 * The Move mode's sheet: how far the pad has travelled down (or in towards the player) since it was
 * centred, from the accelerometer, as a real bar is pulled in.
 *
 * A pad has only a gyro and an accelerometer, so its position is the acceleration integrated twice
 * and drifts. What keeps it usable:
 * - gravity comes off with FMotionBarFilter's "up", leaving the pad's own acceleration;
 * - a zero-velocity update: when the pad is still (gyro and |accel - 1 g| under thresholds for
 *   StillSeconds; once still, only a reading well past them ends it, so tremor does not) its speed
 *   is set to zero, and what the accelerometer reads then is learnt as its bias;
 * - a small deadband, a leak on the speed (fast once the acceleration has stayed inside the
 *   deadband for a moment, slow otherwise), and stops at the ends of the bar's throw;
 * - optionally a slow relaxation of the travel towards a target (the bar's middle).
 * Slow strokes (much over a second end to end) look like the pad being still and are lost, and an
 * accelerometer bias above StillAccelG never lets it settle: see docs/movement.md.
 *
 * Pure: no engine state. Units are SI inside and centimetres at the getters.
 */
class KITESURF_API FMotionBarStroke
{
public:
	/** Back to no travel and no speed, at rest. The learnt bias is kept. */
	void Reset();

	/** Also forgets the learnt accelerometer bias. */
	void ResetBias() { BiasG = FVector::ZeroVector; }

	/** One reading. Up is FMotionBarFilter::GetUp() after that filter has taken the same reading. */
	void Update(const FKiteMotionSample& Sample, const FVector& Up, float DeltaTime);

	/** Travel since the last Reset, down or towards the player positive, between the stops (cm). */
	float GetDisplacementCm() const;

	/** Speed of that travel (cm/s). */
	float GetVelocityCmS() const { return VelocityMS * 100.0f; }

	/** True while the zero-velocity update holds the pad as still. */
	bool IsStill() const { return StillTimeSeconds >= StillSeconds; }

	/** The accelerometer bias learnt while still, in the controller's axes (g). */
	const FVector& GetBiasG() const { return BiasG; }

	/** How much moving the pad towards the player counts, next to moving it down (1: the same). */
	float TowardsWeight = 1.0f;

	/** Still: the gyro reads under this (rad/s) ... */
	float StillGyroRadS = 0.25f;
	/** ... and the accelerometer within this of 1 g (g) ... */
	float StillAccelG = 0.04f;
	/** ... for this long (s). */
	float StillSeconds = 0.2f;
	/** Once still, only a reading past these ends it (g, rad/s): the start of a stroke, not a tremor. */
	float StillBreakAccelG = 0.08f;
	float StillBreakGyroRadS = 0.6f;
	/** When a stroke ends stillness, the readings this far back that pushed the same way count towards it (s). */
	float StillLookbackSeconds = 0.1f;

	/** Acceleration along the pull under this is taken as noise; above it, this much comes off (g). */
	float DeadbandG = 0.01f;

	/** The speed leaks away over this while the pad is accelerating (s): bounds what a bias can do. */
	float VelocityLeakSeconds = 5.0f;

	/** And over this once the acceleration has been inside the deadband for QuietHoldSeconds (s): a drift dies quickly. */
	float QuietLeakSeconds = 0.3f;
	float QuietHoldSeconds = 0.1f;

	/** How fast the bias is learnt while still (s). */
	float BiasLearnSeconds = 0.5f;

	/**
	 * Scales the measured travel. The deadband and the leaks eat about an eighth of a brisk stroke
	 * (a 10 cm stroke in half a second measures about 8.6 cm); this puts it back. Estimate.
	 */
	float StrokeGain = 1.15f;

	/** The travel stays inside these (cm): the bar's stops. */
	float MinDisplacementCm = -1000.0f;
	float MaxDisplacementCm = 1000.0f;

	/** How far past a stop the pad can be taken before the travel stops counting (cm). */
	float StopSlackCm = 3.0f;

	/** Above 0, the travel relaxes towards RelaxTargetCm with this time constant (s). 0: it holds. */
	float RelaxSeconds = 0.0f;
	float RelaxTargetCm = 0.0f;

private:
	float GetMinM() const { return FMath::Min(MinDisplacementCm, MaxDisplacementCm) * 0.01f; }
	float GetMaxM() const { return FMath::Max(MinDisplacementCm, MaxDisplacementCm) * 0.01f; }

	float VelocityMS = 0.0f;
	float DisplacementM = 0.0f;
	float StillTimeSeconds = 0.0f;
	float QuietTimeSeconds = 0.0f;
	/** While still: the last readings along the pull, for StillLookbackSeconds. */
	struct FRecentAlong
	{
		float AlongG;
		float DeltaTime;
		/** The bias before this reading was learnt into it. */
		FVector BiasBefore;
	};
	TArray<FRecentAlong> RecentAlong;
	FVector BiasG = FVector::ZeroVector;
};

namespace KiteMotionBar
{
	/** The controller's own sensors on this platform, or null where they cannot be read. */
	KITESURF_API TSharedPtr<IKiteMotionSource> CreatePlatformSource();
}
