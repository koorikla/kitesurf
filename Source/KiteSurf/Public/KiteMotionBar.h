#pragma once

#include "CoreMinimal.h"

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

	/** Tilt of the top of the pad towards the player, as pulling a bar in: towards is positive (deg). */
	float GetPitchDeg() const;

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
};

namespace KiteMotionBar
{
	/** The controller's own sensors on this platform, or null where they cannot be read. */
	KITESURF_API TSharedPtr<IKiteMotionSource> CreatePlatformSource();
}
