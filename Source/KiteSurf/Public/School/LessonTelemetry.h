#pragma once

#include "CoreMinimal.h"
#include "BoardMovementComponent.h"
#include "LessonTelemetry.generated.h"

/**
 * The kite school's telemetry (docs/tutorials.md 3.3 and 3.4): snapshots of the ride about 60
 * times a second, kept in a fixed-size ring buffer so lesson objectives and fault rules can look
 * at the last seconds of a ride. Pure data: nothing in the game fills it yet. The lesson director
 * (S3) is to poll the board's and the kite's public getters after the trick tracker and add one
 * FLessonSample every fourth 240 Hz fixed step.
 *
 * Units are SI: metres, m/s, degrees, newtons, seconds of board simulation time.
 */

/** One value a lesson can read from a sample, directly or derived from neighbouring samples. */
UENUM(BlueprintType)
enum class ELessonChannel : uint8
{
	/** Horizontal speed over the water (m/s). */
	Speed,
	/** Direction of travel over the water, yaw (deg). Interpolated the short way round. */
	Heading,
	/** Riding direction across the wind: +1 or -1, 0 when unknown. */
	Tack,
	/** Edge input, -1..1 (UBoardMovementComponent::GetEdgeInput). */
	Edge,
	/** |Edge|: how hard the rider edges on either side. */
	EdgeAbs,
	/** Rider height above the water (m). */
	Height,
	/** Vertical speed, positive up (m/s). */
	VerticalSpeed,
	/** Kite clock around the window axis (deg, 0 at 12, + to the right looking downwind). */
	KiteClock,
	/** |KiteClock|: how far the kite is from 12. */
	KiteClockAbs,
	/** Kite elevation above the horizon (deg). */
	KiteElevation,
	/** Kite position minus rider position along the downwind direction (m); below 0 the kite is upwind of the rider. */
	KiteDownwind,
	/** Angle between the direction of travel and the horizontal direction to the kite (deg, 0..180): 0 the nose points at the kite. */
	KiteBearing,
	/** Line tension (N). */
	Tension,
	/** Bar position, 0 sheeted right out to 1 sheeted fully in. */
	Bar,
	/** Steering input, -1..1. */
	Steer,
	/** 1 while the board state is Planing, else 0. */
	Planing,
	/** 1 while the board state is Airborne, else 0. */
	Airborne,
	/** 1 while the rider or the kite has crashed (a fall), else 0. */
	Fallen,
	/** 1 while riding toeside, else 0. Always 0 until toeside riding exists (tricks T3.7). */
	Toeside,
	/** Completed kite loops so far (a running count). */
	CompletedLoops,
	/** Rider position along the upwind direction (m): grows as the rider gains ground upwind. */
	Upwind,
	/** Derived: rate of change of the kite elevation from the previous sample (deg/s, + climbing; 0 for the oldest). */
	KiteClimbRate,
	/**
	 * Derived: the bar position on samples where the kite climbs faster than
	 * LessonTelemetry::ClimbRateThresholdDegS below LessonTelemetry::ClimbCeilingDeg, otherwise 0.
	 * Sheeting in while the kite climbs is the classic small-jump mistake (lesson B2).
	 */
	BarWhileClimbing
};

/** One fixed simulation step as the lesson director sees it. */
USTRUCT(BlueprintType)
struct FLessonSample
{
	GENERATED_BODY()

	/** Board simulation time (s, UBoardMovementComponent::GetSimTimeSeconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float TimeSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	EBoardState BoardState = EBoardState::Displacement;

	/** Horizontal speed over the water (m/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float SpeedMS = 0.0f;

	/** Direction of travel, yaw (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float HeadingDeg = 0.0f;

	/** +1 or -1 by which way the rider crosses the wind, 0 when going nowhere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	int32 Tack = 0;

	/** Edge input, -1..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float EdgeInput = 0.0f;

	/** Height above the water (m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float HeightM = 0.0f;

	/** Vertical speed, + up (m/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float VerticalSpeedMS = 0.0f;

	/** UKiteComponent::GetClockDeg (deg, 0 at 12). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float KiteClockDeg = 0.0f;

	/** UKiteComponent::GetElevationDeg (deg above the horizon). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float KiteElevationDeg = 90.0f;

	/** Kite minus rider along the downwind direction (m); negative when the kite is upwind of the rider (a front stall). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float KiteDownwindM = 0.0f;

	/** Angle between the direction of travel and the horizontal direction to the kite (deg, 0..180). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float KiteBearingDeg = 90.0f;

	/** UKiteComponent::GetLineTensionN (N). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float TensionN = 0.0f;

	/** Bar position, 0 out to 1 in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float BarPosition = 0.0f;

	/** Steering input, -1..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float SteerInput = 0.0f;

	/** Rider position along the upwind direction (m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float UpwindM = 0.0f;

	/** Completed kite loops so far. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	int32 CompletedLoops = 0;

	/** The rider fell or the kite is down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	bool bFallen = false;

	/** Riding toeside (false until tricks T3.7). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	bool bToeside = false;

	/** The raw value of a non-derived channel (KiteClimbRate and BarWhileClimbing need neighbours: use FLessonTelemetry::ChannelAt). */
	float Get(ELessonChannel Channel) const;
};

/** Min, max and mean of a channel over a time window. */
struct FLessonWindowStats
{
	float Min = 0.0f;
	float Max = 0.0f;
	float Mean = 0.0f;
	/** Values that went into the stats: the samples inside the window plus its interpolated ends. */
	int32 Count = 0;

	bool IsValid() const { return Count > 0; }
};

namespace LessonTelemetry
{
	/** A kite climbing faster than this counts as climbing for BarWhileClimbing (deg/s). */
	inline constexpr float ClimbRateThresholdDegS = 5.0f;
	/** Above this elevation the kite is at 12, where sheeting in is right (deg). */
	inline constexpr float ClimbCeilingDeg = 80.0f;
	/** The ring buffer's default size: 25 s at 60 Hz, so a 20 s hold (lesson A3) fits with room to spare. */
	inline constexpr int32 DefaultCapacity = 1500;
}

/**
 * The last samples of a ride, oldest first, in a ring buffer allocated once. Times must go up:
 * a sample at or before the newest one is refused. Queries take times on the samples' clock.
 *
 * Window queries count the samples inside [From, To] plus values interpolated at From and To
 * when those fall inside the buffer, so a window shorter than a step still has a value. Time in
 * band holds each sample's value until the next sample.
 */
class KITESURF_API FLessonTelemetry
{
public:
	explicit FLessonTelemetry(int32 InCapacity = LessonTelemetry::DefaultCapacity);

	/** Appends a sample, overwriting the oldest when full. False (and nothing added) when its time is not after the newest. */
	bool Add(const FLessonSample& Sample);

	/** Forgets every sample; the capacity stays. */
	void Reset();

	int32 Num() const { return Count; }
	int32 GetCapacity() const { return Samples.Num(); }
	bool IsEmpty() const { return Count == 0; }

	/** Sample by age: 0 is the oldest, Num() - 1 the newest. */
	const FLessonSample& Get(int32 Index) const;
	const FLessonSample& Latest() const { return Get(Count - 1); }

	float OldestTime() const { return Count > 0 ? Get(0).TimeSeconds : 0.0f; }
	float LatestTime() const { return Count > 0 ? Latest().TimeSeconds : 0.0f; }
	/** Time from the oldest to the newest sample (s). */
	float SpanSeconds() const { return LatestTime() - OldestTime(); }

	/** A channel's value at a sample, derived channels included. */
	float ChannelAt(int32 Index, ELessonChannel Channel) const;

	/** Index of the newest sample at or before Time; INDEX_NONE when every sample is later (or there are none). */
	int32 FindIndexAtOrBefore(float TimeSeconds) const;

	/** A channel at a time, interpolated between the samples either side (Heading the short way round). False outside the buffer. */
	bool ValueAt(ELessonChannel Channel, float TimeSeconds, float& OutValue) const;

	/** Min, max and mean of a channel over [From, To]. Invalid (Count 0) when nothing of the window is in the buffer. */
	FLessonWindowStats Window(ELessonChannel Channel, float FromSeconds, float ToSeconds) const;

	/** Seconds within [From, To] that the channel spent in [Min, Max], each sample held until the next. */
	float TimeInBand(ELessonChannel Channel, float Min, float Max, float FromSeconds, float ToSeconds) const;

	/**
	 * How long the newest samples have met a test without a break (s): newest time minus the time
	 * of the first sample of the unbroken run that ends at the newest sample. 0 when the newest
	 * sample fails, or when the run is one sample long. Samples before NotBeforeSeconds end the run.
	 */
	float TrailingTimeWhere(TFunctionRef<bool(int32 Index)> Test, float NotBeforeSeconds = -UE_BIG_NUMBER) const;

	/** TrailingTimeWhere with the test a channel inside [Min, Max]. */
	float TrailingTimeInBand(ELessonChannel Channel, float Min, float Max, float NotBeforeSeconds = -UE_BIG_NUMBER) const;

	/**
	 * Times the tack changed: the time of the first sample on the new tack, oldest first. Samples
	 * with tack 0 are skipped, so -1, 0, +1 is one change at the +1 sample.
	 */
	TArray<float> FindTackChanges() const;

private:
	int32 Physical(int32 Index) const { return (Head + Index) % Samples.Num(); }

	TArray<FLessonSample> Samples;
	/** Physical index of the oldest sample. */
	int32 Head = 0;
	int32 Count = 0;
};
