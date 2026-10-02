#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WindStreakComponent.generated.h"

class UBoardMovementComponent;
class UInstancedStaticMeshComponent;
class UWindComponent;

/** One streak of wind on the water. */
struct FWindStreak
{
	/** Where it is on the water (cm). */
	FVector2D Position = FVector2D::ZeroVector;
	/** 0..1 along its life at the edge of the field: how much of its full opacity it has. */
	float Opacity = 0.0f;
	/** Length against the component's StreakLengthCm, so they are not all alike. */
	float LengthScale = 1.0f;
};

/**
 * Wind lines on the sea: long thin streaks of foam lying along the wind and drifting down it, in
 * a field that follows the rider. They are how the wind's direction is read off the water. Like
 * the wake, a small CPU-simulated pool drawn as instanced meshes.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class KITESURF_API UWindStreakComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWindStreakComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Moves the streaks with the wind and keeps the field round the owner; public so tests can step it. */
	void Simulate(float DeltaTime);

	const TArray<FWindStreak>& GetStreaks() const { return Streaks; }

	/** The way the streaks lie: the wind's direction over the water (deg). */
	float GetStreakYawDeg() const { return StreakYawDeg; }

	/** How strongly the streaks show for this wind, 0..1: none in a calm, full from FullStrengthWindKnots. */
	UFUNCTION(BlueprintPure, Category = "Wind Streaks")
	float GetStrengthForWind(float WindKnots) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Streaks", meta = (ClampMin = "1"))
	int32 StreakCount;

	/** The field of streaks reaches this far from the rider (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Streaks")
	float FieldRadiusCm;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Streaks")
	float StreakLengthCm;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Streaks")
	float StreakWidthCm;

	/** The streaks drift downwind at this fraction of the wind speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Streaks")
	float DriftFraction;

	/** Opacity of a streak in the middle of the field at full strength. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Streaks", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxOpacity;

	/** Below this wind there are no streaks; they build from here (knots). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Streaks")
	float MinWindKnots;

	/** Wind at which the streaks are at their strongest (knots). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Streaks")
	float FullStrengthWindKnots;

	/** How far above the water surface the streaks are drawn, to stay clear of it (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Streaks")
	float HeightAboveWaterCm;

protected:
	virtual void BeginPlay() override;

private:
	void UpdateInstances();

	UPROPERTY(Transient)
	TObjectPtr<UWindComponent> Wind;

	UPROPERTY(Transient)
	TObjectPtr<UBoardMovementComponent> BoardMovement;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> Instances;

	TArray<FWindStreak> Streaks;
	FRandomStream Random;
	float StreakYawDeg = 0.0f;
	float Strength = 0.0f;
	bool bSeeded = false;
};
