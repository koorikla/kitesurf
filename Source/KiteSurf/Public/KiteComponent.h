#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "KiteGear.h"
#include "Tricks/KiteLoopTracker.h"
#include "KiteComponent.generated.h"

class UWindComponent;
class UStaticMeshComponent;
class UCableComponent;

/** What the air did to the kite in its last fixed step, for debug drawing and telemetry. Winds in cm/s, forces in N, world frame. */
struct FKiteStepDebug
{
	/** True wind at the kite. */
	FVector TrueWindCmS = FVector::ZeroVector;
	/** Air flowing past the kite: the true wind minus the kite's own velocity. */
	FVector ApparentWindCmS = FVector::ZeroVector;
	FVector LiftN = FVector::ZeroVector;
	FVector DragN = FVector::ZeroVector;
	/** The side force that resists the kite sliding sideways through the air. */
	FVector SideN = FVector::ZeroVector;
	/** Line tension the step needed, before MaxLineTensionN caps what the rider feels. */
	float TensionN = 0.0f;
	float AlphaDeg = 0.0f;
	float LiftCoefficient = 0.0f;
	float DragCoefficient = 0.0f;
	bool bTaut = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnKiteCrashed, FVector, WaterLocation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnKiteRelaunched);

/**
 * The kite: a point mass on the end of its lines.
 *
 * It has a position, a velocity and a heading (where its nose points). Each step it feels
 * - lift and drag from the air flowing over it (the true wind minus its own velocity), set by
 *   its angle of attack: the angle the flow makes with the canopy, plus the trim from the bar;
 * - a side force that resists sliding sideways, which is what makes it fly where it points;
 * - gravity; and
 * - the pull of the lines, which only ever pull: they hold the kite at line length while the
 *   forces on it point away from the rider, and go slack when they do not.
 *
 * Line tension is whatever the lines need to hold the kite on its arc, so it rises with the
 * square of the kite's airspeed and falls to nothing in a lull. With slack lines nothing holds
 * the canopy to the wind: the kite stops flying, drifts and falls like a sheet until the lines
 * come tight again or it reaches the water. Too much angle of attack stalls it; air on the wrong
 * side of the canopy (overflying the window, or outrunning the wind) luffs it.
 *
 * Steering turns the nose at a rate proportional to airspeed, so the tightest turn has the same
 * radius at any speed; a depowered kite turns more slowly, and the bar reaches the canopy only
 * after a dead time that grows as the bar goes out. A slow kite flying across the window drops its
 * nose. Normally an assist flies it: bar over means travel round the window that way; bar centred
 * lets the kite drift up the window edge to the zenith and sit there, as a real kite does with the
 * bar neutral (or, with bParkHoldAssist, holds it where the bar was centred). With the rider in the
 * air (SetRiderAirborne) bar centred flies the kite to the zenith over them and holds it there. The
 * assist judges where the window is from the wind the rider feels across the water: the true wind
 * less their horizontal velocity. With the loop input held the bar turns the kite directly, so
 * holding it flies a loop. In the air a full bar does the same from anywhere in the window, and
 * reversed mid-loop starts a loop the other way (AirLoopFullBarThreshold).
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class KITESURF_API UKiteComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UKiteComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Advances the kite by DeltaTime in sub-steps no longer than MaxStepSeconds. For tests and for a kite nobody else steps. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void UpdateKite(float DeltaTime);

	/** One fixed step of the kite, with the rider where they are now. The pawn calls this inside its own step loop. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void StepKite(float StepSeconds);

	/** Time the kite's simulation has advanced (s); the wind is sampled at this time. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetSimTimeSeconds() const { return SimTimeSeconds; }

	/** Places the mesh and lines. Alpha blends from the previous step's position to the current one, for rendering between steps. */
	void UpdateVisuals(float Alpha = 1.0f);

	// Inputs
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SteerKite(float Axis /* -1..1 */);

	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SheetKite(float Amount /* 0..1 */);

	/**
	 * Forces the bar straight through to the kite wherever it is in the window. Not bound to a
	 * key: the rider loops the kite by holding the bar towards the kite's own side. This is for
	 * scripted input and tests that need raw steering from any position.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SetLoopHeld(bool bHeld);

	UFUNCTION(BlueprintCallable, Category = "Kite")
	bool IsLoopHeld() const { return bLoopHeld; }

	/** True while the rider's bar is going straight to the kite and turning it round: a loop in progress. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	bool IsLooping() const { return bLooping; }

	/** How far round the window on its own side (deg of clock) the kite must be for the bar held that way to loop it rather than fly it there. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Steering", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float LoopClockDeg;

	/**
	 * With the rider in the air (SetRiderAirborne), a bar at least this far over (of 1, the bar as it
	 * reaches the kite after the dead time) loops the kite its way from anywhere in the window, not
	 * only from LoopClockDeg round on its own side; reversed to it mid-loop it ends that loop and
	 * starts one the other way. That is what flies S-loops and contra loops (docs/tricks/T2.md T2.4).
	 * A smaller bar still flies the kite across, and a full bar already held when the rider leaves
	 * the water (the send) does not count until it has been eased under this or pulled the other
	 * way, so a send held through the take-off flies as before. The full bar has to be held for
	 * AirLoopHoldSeconds first. On the water the LoopClockDeg rule alone applies. Estimate.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Steering", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float AirLoopFullBarThreshold;

	/**
	 * How long a full bar (AirLoopFullBarThreshold, the bar as it reaches the kite) must be held in
	 * the air before it starts or reverses a loop (s). A shorter tap flies the kite across as any
	 * other bar does, so arrow-key steering (always a full bar) can still fly the kite in the air.
	 * Estimate.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Steering", meta = (ClampMin = "0.0", Units = "s"))
	float AirLoopHoldSeconds;

	/** The kite sizes on offer (m^2), smallest first. */
	static TConstArrayView<float> GetKiteSizesM2();

	/** The size from GetKiteSizesM2 a rider of this weight would rig for this wind: about 2.2 x kg / knots. */
	UFUNCTION(BlueprintPure, Category = "Kite")
	static float RecommendKiteSizeM2(float WindKnots, float RiderMassKg = 85.0f);

	/**
	 * Rigs a kite of this area. Its mass, the air it has to push and its turning circle follow the
	 * size: a small kite is light and turns tightly, a big one is slow and powerful.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SetKiteSize(float InAreaM2);

	/** Rigs this kind of kite at the current size: its lift, glide, weight and turning follow the model. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SetKiteModel(EKiteModel InModel);

	UFUNCTION(BlueprintPure, Category = "Kite")
	EKiteModel GetKiteModel() const { return KiteModel; }

	/** Linear size of this kite against the 12 m^2 the mesh and the tuning are built for. */
	UFUNCTION(BlueprintPure, Category = "Kite")
	float GetSizeScale() const { return FMath::Sqrt(FMath::Max(AreaM2, 1.0f) / ReferenceAreaM2); }

	static constexpr float ReferenceAreaM2 = 12.0f;

	/**
	 * The steering actually reaching the kite, -1..1: the rider's bar while looping, otherwise what
	 * the assist asks for to carry out the bar's intent. 0 while the kite is in the water. The
	 * rider's bar reaches the kite GetSteeringDeadTimeSeconds() after it moves.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetAppliedSteer() const { return AppliedSteer; }

	/** How long the bar takes to reach the canopy at the current sheet (s): SteeringDeadTimeSeconds, plus DepoweredDeadTimeExtraSeconds with the bar right out. */
	UFUNCTION(BlueprintPure, Category = "Kite")
	float GetSteeringDeadTimeSeconds() const;

	/** True while the kite is lying on the water. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	bool IsCrashed() const { return bCrashed; }

	/** Seconds until a crashed kite relaunches by itself; 0 when flying. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetRelaunchSecondsRemaining() const { return bCrashed ? FMath::Max(RelaunchDelaySeconds - CrashedSeconds, 0.0f) : 0.0f; }

	UPROPERTY(BlueprintAssignable, Category = "Kite|Events")
	FOnKiteCrashed OnKiteCrashed;

	UPROPERTY(BlueprintAssignable, Category = "Kite|Events")
	FOnKiteRelaunched OnKiteRelaunched;

	// Outputs
	UFUNCTION(BlueprintCallable, Category = "Kite")
	FVector GetLineForce() const; // Force on rider in kg*cm/s^2 (1 N = 100 kg*cm/s^2)

	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetLineTensionN() const; // Line tension in Newtons

	UFUNCTION(BlueprintCallable, Category = "Kite")
	FVector GetKiteWorldPosition() const;

	/** Angle of the kite to the right of straight downwind (true wind), -180..180. A fast rider's kite can sit past 90. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetAzimuthDeg() const;

	/** Angle of the kite above the horizon, 0..90. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetElevationDeg() const;

	UFUNCTION(BlueprintCallable, Category = "Kite")
	FVector GetKiteVelocity() const;

	UFUNCTION(BlueprintCallable, Category = "Kite")
	FRotator GetKiteRotation() const;

	/** Unit vector along the sphere in the direction the kite's nose points. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	FVector GetKiteHeading() const { return KiteHeading; }

	/** Speed of the air over the kite (cm/s). */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetAirspeedCmS() const { return AirspeedCmS; }

	/** Angle of the airflow to the canopy, including bar trim (deg). Above StallAngleDeg the kite is stalled; below zero it is luffing. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetAngleOfAttackDeg() const { return AngleOfAttackDeg; }

	/** The air's forces on the kite in the last fixed step: winds, lift, drag, side force, tension, angle of attack and coefficients. */
	const FKiteStepDebug& GetLastStepDebug() const { return LastStepDebug; }

	/** True while the lines are tight. With slack lines the kite is not flying and the rider feels nothing. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	bool AreLinesTaut() const { return bLinesTaut; }

	/** Degrees the kite has turned under the current steering input; 360 is one loop. Positive to the right. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetTurnDeg() const { return TurnDeg; }

	/**
	 * The kite's own heading turn in its last fixed step (deg, + to the right, the sign of GetTurnDeg),
	 * looping or not: the steering, weathercock and gravity turn of the nose, not a difference of
	 * headings. 0 with slack lines and on the water. This is what the loop records are built from.
	 */
	UFUNCTION(BlueprintPure, Category = "Kite|Loops")
	float GetLastStepTurnDeg() const { return LastStepTurnDeg; }

	/** The side the loop in progress is flown to: +1 right, -1 left (the sign of GetTurnDeg); 0 when not looping. */
	UFUNCTION(BlueprintPure, Category = "Kite|Loops")
	float GetLoopSide() const { return bLooping ? LoopSide : 0.0f; }

	/**
	 * The kite's loop records, oldest first (docs/tricks/T0.md section 3): every whole loop and every
	 * unfinished one of at least half a turn, built from GetLastStepTurnDeg each fixed step by an
	 * FKiteLoopTracker, so they do not depend on IsLooping. Times are on the kite's clock
	 * (GetSimTimeSeconds). Placing the kite (a reset, a relaunch) drops the open run, not the records.
	 */
	const TArray<FKiteLoopRecord>& GetLoopRecords() const { return LoopTracker.GetRecords(); }

	/** Loop records made so far, including any no longer kept: goes up by one per record. */
	UFUNCTION(BlueprintPure, Category = "Kite|Loops")
	int32 GetLoopRecordCount() const { return LoopTracker.GetTotalRecorded(); }

	/** The loop being flown now as a provisional, incomplete record (turn since its last whole loop); false when no run is open. */
	UFUNCTION(BlueprintPure, Category = "Kite|Loops")
	bool GetOpenLoop(FKiteLoopRecord& OutLoop) const { return LoopTracker.GetOpenRun(OutLoop); }

	/** Direction of the open loop run: +1 right, -1 left, 0 when none is open. */
	UFUNCTION(BlueprintPure, Category = "Kite|Loops")
	int32 GetLoopDirection() const { return LoopTracker.GetRunDirection(); }

	/** The loop tracker the kite steps, for its settings and the open run's total turn. */
	const FKiteLoopTracker& GetLoopTracker() const { return LoopTracker; }

	/** Place the kite parked at this azimuth (keeps elevation). */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SetAzimuthDeg(float InAzimuthDeg);

	/** Place the kite parked at this elevation (keeps azimuth). */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SetElevationDeg(float InElevationDeg);

	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SetWindComponent(UWindComponent* InWindComponent);

	/**
	 * Place the kite by clock position and depth in the window the rider feels (see GetWindowAxis).
	 * ClockDeg: angle from the zenith around the window axis, positive to the right looking down it (90 = right horizon).
	 * DepthDeg: angle from the window edge towards the middle of the window.
	 * The kite then settles to the depth its lift and drag give it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SetWindowPosition(float ClockDeg, float DepthDeg);

	/** Clock position of the kite around the window axis, positive to the right looking downwind. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetClockDeg() const;

	/** Angle of the kite from the window edge towards the middle of the window. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetWindowDepthDeg() const;

	/** Horizontal unit vector the true wind blows towards, sampled at the rider (RiderWindHeightCm above their feet). */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	FVector GetDownwindDir() const;

	/** Horizontal unit vector the apparent wind (true wind minus rider velocity) blows towards: the axis of the window the rider feels. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	FVector GetWindowAxis() const;

	/** Where the rider feels the wind: this far above their feet (cm), about chest height. The window axis and the downwind direction are sampled here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Config", meta = (ClampMin = "0.0"))
	float RiderWindHeightCm;

	/** Where each line meets the bar, in front of the rider on the kite's side. */
	FVector GetBarEndWorldPosition(bool bLeft) const;

	/** The rider holds the bar: they say where its ends are, and the lines run from there to the kite. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SetBarEnds(const FVector& LeftEnd, const FVector& RightEnd);

	// Visual Components
	UStaticMeshComponent* GetKiteMesh() const { return KiteMesh; }
	UCableComponent* GetLeftLine() const { return LeftLine; }
	UCableComponent* GetRightLine() const { return RightLine; }
	UCableComponent* GetLeftCenterLine() const { return LeftCenterLine; }
	UCableComponent* GetRightCenterLine() const { return RightCenterLine; }

	/** Derived from the kite's position each update; use the setters to place the kite. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Kite|State")
	float AzimuthDeg;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Kite|State")
	float ElevationDeg;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Config", meta = (ClampMin = "100.0"))
	float LineLengthCm; // Default 2400 = 24m

	/** Flat area (m^2): the size a kite is sold by. Mass, added mass, turning circle and the mesh follow it; the air acts on GetProjectedAreaM2(). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Config", meta = (ClampMin = "1.0"))
	float AreaM2; // Default 12 m^2

	/**
	 * Share of the flat area the arched canopy presents to the air: lift, drag and the side force
	 * act on AreaM2 times this (a collapsed canopy drags on its full area, SlackDragCoefficient).
	 * Research: 0.65 to 0.80, typically 0.72 (docs/physics/research.md 1.7); 0.69 puts a 9 m^2 kite
	 * at the zenith in 30 kn at 1.09 kN (research 0.9 to 1.1). 1 flies on the flat area, as the model
	 * did before phase 2.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Aerodynamics", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float ProjectedAreaRatio;

	/** The area the air acts on (m^2): AreaM2 * ProjectedAreaRatio. */
	UFUNCTION(BlueprintPure, Category = "Kite")
	float GetProjectedAreaM2() const { return AreaM2 * ProjectedAreaRatio; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Config", meta = (ClampMin = "0.1"))
	float MassKg; // Default 3 kg

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|State", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Sheet; // 0..1

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|State", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Steer; // -1..1

	/** Mass of air the kite has to push around as it accelerates (kg), added to MassKg for inertia. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Aerodynamics")
	float AddedMassKg;

	/**
	 * Lift coefficient at the stall, the most the canopy can make. Research: 1.0 to 1.2 at 16 to 20 deg
	 * (docs/physics/research.md 1.2). SetKiteSize and SetKiteModel re-rig it as 1.1 times the model's
	 * lift scale; before phase 2 it was 1.2.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Aerodynamics")
	float MaxLiftCoefficient;

	/**
	 * Angle of attack at which the kite stalls (deg). 20: the top of the research's 16 to 20, so a
	 * kite overhead in the air keeps flying as the rider's sink raises its angle of attack. Before
	 * phase 2 it was 18.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Aerodynamics")
	float StallAngleDeg;

	/**
	 * Angle of attack at which the canopy makes no lift (deg): attached-flow lift follows the angle
	 * above it, reaching MaxLiftCoefficient at StallAngleDeg. A cambered tube kite still lifts at zero
	 * angle: research puts this at about -3 deg (docs/physics/research.md 1.2). Before phase 2 it was 0
	 * (no camber), with the trims 2 to 3 deg higher doing its job.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Aerodynamics", meta = (ClampMin = "-15.0", ClampMax = "5.0"))
	float ZeroLiftAngleDeg;

	/** How far the force on a stalled canopy leans towards its nose (deg). More recovers from a stall faster. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Aerodynamics")
	float StalledForwardTiltDeg;

	/** Drag coefficient at zero lift: canopy, tubes and lines. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Aerodynamics")
	float ParasiteDragCoefficient;

	/** Drag due to lift: Cd rises by this times Cl squared. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Aerodynamics")
	float InducedDragFactor;

	/**
	 * A kite bent into a turn is draggy, which keeps loop speed down: while the flow is attached the
	 * drag coefficient is multiplied by 1 + this times the steering reaching the canopy (0..1); a
	 * stalled canopy drags the same however it is bent. Research: 0.6 (Fechner et al. 2015,
	 * docs/physics/research.md 1.4).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Aerodynamics", meta = (ClampMin = "0.0"))
	float SteeringDragFactor;

	/** Resistance to sliding sideways through the air; makes the kite fly where it points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Aerodynamics")
	float SideForceCoefficient;

	/** Drag coefficient of a kite that is not flying (slack lines), on its full area. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Aerodynamics")
	float SlackDragCoefficient;

	/** Slack in the lines (cm) at which the canopy has lost its shape completely and only drags. With less it still flies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Aerodynamics", meta = (ClampMin = "1.0"))
	float SlackCollapseCm;

	/**
	 * Canopy trim angle with the bar out (deg): depowered. Added to the angle the flow meets the canopy
	 * at; parked at the window edge in 20 kn that is about 10.8 deg, so -15.4 gives -4.6 deg of angle
	 * of attack there, a luff margin rather than a collapse (the kite then settles deeper in the
	 * window at about +2 deg). Before phase 2 it was -22.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Aerodynamics")
	float TrimSheetedOutDeg;

	/**
	 * Canopy trim angle with the bar in (deg): powered. 3.6 gives 14.4 deg of angle of attack parked
	 * at the window edge in 20 kn, 5.6 deg short of the stall, and puts the bar's throw at 19 deg
	 * (research 12 to 20, docs/physics/research.md 1.5). Before phase 2 it was +2.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Aerodynamics")
	float TrimSheetedInDeg;

	/** Radius of the tightest turn, at full steering (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float MinTurnRadiusCm;

	/** How quickly the kite winds into a turn (1/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float TurnResponse;

	/** How strongly the nose swings round to face the air it is flying through (rad/s per m/s of sideways flow). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float WeathercockGain;

	/**
	 * How hard gravity turns the nose of a slow kite towards the water (rad m/s^2): the heading rate
	 * is this over the airspeed, times how far the kite is flying across the window and how low it
	 * is (sin(heading) cos(elevation)). For the 12 m^2 reference kite; smaller kites get more, by
	 * sqrt(ReferenceAreaM2 / AreaM2), as they turn more tightly. Research: Fechner's c_2, 6.28 for a
	 * 10 m^2 kite (Fechner et al. 2015, docs/physics/research.md 1.4), the value to tune towards. The
	 * default 2.4 is the most the park-hold assist holds its clock position against (within 4 deg);
	 * the old gravity term was 1.5.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Steering", meta = (ClampMin = "0.0"))
	float GravityTurnRadMPerS2;

	/**
	 * Dead time between the bar moving and the canopy answering, with the bar right in (s).
	 * Research: about 0.2 s at full power (Elfert, Goehlich and Schmehl 2024, docs/physics/research.md 1.4).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Steering", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float SteeringDeadTimeSeconds;

	/**
	 * Extra dead time with the bar right out (s), scaled by how far out it is: a depowered kite is
	 * slow to answer the bar. Research: the dead time grows to about 0.6 s at minimum power (Elfert 2024).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Steering", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float DepoweredDeadTimeExtraSeconds;

	/**
	 * Turn rate from the bar with the bar right out, as a fraction of the turn rate with it right in;
	 * in between it follows the sheet. Research: the steering gain falls from 0.35 to 0.15 rad/m from
	 * full to minimum power, about 0.45 (Elfert 2024, docs/physics/research.md 1.4).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Steering", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DepoweredTurnRateFactor;

	/** Heading the assist asks for at full steering, measured from nose-out: just past along-the-edge, so the kite also dips into the window and gains power as it travels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float TravelHeadingDeg;

	/** Bar the assist applies per radian of heading error. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float SteerAssistGain;

	/**
	 * With the bar centred the kite drifts up the window edge to the zenith (12 o'clock): the assist
	 * leans its nose towards 12 by this many degrees per degree of clock it still has to go...
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning", meta = (ClampMin = "0.0"))
	float ZenithDriftGain;

	/** ...up to this much (deg of nose lean). More climbs faster. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float ZenithDriftMaxHeadingDeg;

	/** With the bar centred, hold the kite at the clock position it had when the bar was centred, instead of letting it drift up to 12. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Assist")
	bool bParkHoldAssist;

	/**
	 * With bParkHoldAssist, degrees of heading correction per degree the kite has drifted from where
	 * the bar was centred... 2.5 holds a kite parked low at the edge within 4 deg of its clock; it was
	 * 2 before phase 2, when the kite flew on its flat area and its weight counted for less.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float ParkHoldGain;

	/** ...up to this much (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float ParkHoldMaxDeg;

	/**
	 * The rider says each step whether they are in the air (the pawn does, from the board). In the air
	 * with the bar centred the assist flies the kite to 12 o'clock over them and holds it there
	 * (AirborneZenithGain), whatever bParkHoldAssist says: a kite overhead is what carries a rider
	 * through a jump. A bar over by less than AirLoopFullBarThreshold still travels, a full bar loops
	 * the kite its way from anywhere in the window (AirLoopFullBarThreshold), bar towards the kite's
	 * own side still loops, and the floor rule still applies.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SetRiderAirborne(bool bAirborne) { bRiderAirborne = bAirborne; }

	UFUNCTION(BlueprintPure, Category = "Kite")
	bool IsRiderAirborne() const { return bRiderAirborne; }

	/**
	 * With the rider in the air and the bar centred, the assist leans the kite's nose towards 12 by
	 * this many degrees per degree of clock it still has to go... (on the water, ZenithDriftGain). At 2
	 * a kite sent for the timed jump in 30 kn reaches 12 about 1.5 s after take-off and is above 60 deg
	 * by 2.5 s; more gain does not get it there sooner (the rider's climb is what it waits for), and
	 * at 6 it overshoots and stalls. 0 turns the hold off: bar centred then does in the air what it
	 * does on the water (drift to 12, or bParkHoldAssist), as before phase 2.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Assist", meta = (ClampMin = "0.0"))
	float AirborneZenithGain;

	/** ...up to this much (deg of nose lean). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Assist", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float AirborneZenithMaxHeadingDeg;

	/** The assist will not fly the kite lower than this; a loop, a stall or slack lines can take it lower. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float MinElevationDeg;

	/** The kite has hit the water when it gets this close to it (cm above world Z = 0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float CrashHeightCm;

	/** A kite on the water relaunches by itself after this long, or sooner if steered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float RelaunchDelaySeconds;

	/** Wind needed to get a kite off the water (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float MinRelaunchWindCmS;

	/** Cap on the pull passed to the rider, standing in for line stretch and the rider giving way (N). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float MaxLineTensionN;

	/** Draw this kite's forces and its rider's as kite.Physics.Debug 1 does, whatever the console variable says. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Debug")
	bool bDrawDebug;

	/** The flight is stepped in sub-steps no longer than this (s), whatever UpdateKite is given. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Simulation", meta = (ClampMin = "0.0001"))
	float MaxStepSeconds;

	/** Most sub-steps one UpdateKite call will take; past that the sub-step grows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Simulation", meta = (ClampMin = "1"))
	int32 MaxStepsPerUpdate;

protected:
	UPROPERTY(Transient)
	TObjectPtr<UWindComponent> WindComponent;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> KiteMesh;

	UPROPERTY(Transient)
	TObjectPtr<UCableComponent> LeftLine;

	UPROPERTY(Transient)
	TObjectPtr<UCableComponent> RightLine;

	UPROPERTY(Transient)
	TObjectPtr<UCableComponent> LeftCenterLine;

	UPROPERTY(Transient)
	TObjectPtr<UCableComponent> RightCenterLine;

	void SetupVisuals();

	FVector GetWindAt(const FVector& Location) const;
	FVector GetRiderPosition() const;
	FVector GetRiderVelocity() const;

	/** Direction from the rider for an azimuth and elevation measured from the true wind. */
	FVector DirectionFromAngles(float InAzimuthDeg, float InElevationDeg) const;

	/** Rebuilds the flight state from AzimuthDeg / ElevationDeg: at line length there, moving with the rider, nose-out. */
	void PlaceParked();

	/** Updates KiteDir, AzimuthDeg and ElevationDeg from the kite's position. */
	void UpdateAngles();

	/** One fixed step of the flight dynamics; returns the line tension during the step (N). */
	float StepFlight(float StepSeconds, float SteerInput, const FVector& RiderPos, const FVector& RiderVelocity, const FVector& Wind);

	/** Lift and drag coefficients for an angle of attack in degrees, valid at any angle, with the steering (-1..1) reaching the canopy. */
	void GetAeroCoefficients(float AlphaDeg, float& OutLift, float& OutDrag, float SteerAmount = 0.0f) const;

	/** Steering the kite is flown with, from the rider's bar as it reaches the kite (Bar, after the dead time): the bar itself in loop mode, the assist's otherwise. */
	float ComputeSteering(float DeltaTime, float Bar, const FVector& RiderVelocity, const FVector& Wind);

	/** One bar position and when it was applied, for the steering dead time. */
	struct FSteerSample
	{
		float TimeSeconds = 0.0f;
		float Steer = 0.0f;
	};

	/** Forgets the bar's history: from now on it has been at Steer. */
	void ResetSteerHistory(float Steer);

	/** Remembers the rider's bar at this simulation time, and forgets what no dead time can reach back to. */
	void RecordSteer(float TimeSeconds, float Steer);

	/** Where the bar was at this simulation time, interpolated between the recorded steps. */
	float GetRecordedSteer(float TimeSeconds) const;

	const FSteerSample& GetSteerSample(int32 Index) const { return SteerHistory[(SteerHistoryStart + Index) % SteerHistory.Num()]; }

	/** The bar's recent positions, oldest first from SteerHistoryStart: a ring that grows when full. */
	TArray<FSteerSample> SteerHistory;
	int32 SteerHistoryStart = 0;
	int32 SteerHistoryNum = 0;

	/** The simulation time whose bar the kite last flew with (s). */
	float BarReadSeconds = -UE_BIG_NUMBER;

	void Crash();
	void Relaunch();

	/** Feeds the loop tracker this step: LastStepTurnDeg, the elevation, tension and the rider, and whether the kite is on the water. */
	void StepLoopTracker(const FVector& RiderPos, const FVector& RiderVelocity);

	/** Loop records from the kite's own per-step turn (T0.3). */
	FKiteLoopTracker LoopTracker;

	/** Heading turn in the last fixed step (deg, + right), set in StepFlight whether or not the kite is looping. */
	float LastStepTurnDeg = 0.0f;

	/** Unit vector from the rider to the kite. */
	FVector KiteDir;

	/** Unit vector in the direction of the kite's nose, at right angles to KiteDir. */
	FVector KiteHeading;

	/** World velocity of the kite (cm/s). */
	FVector KiteVelocity;

	FVector KiteWorldPosition;
	FRotator KiteWorldRotation;

	/** Where the kite was before the last step, for drawing it between steps. */
	FVector PrevKiteWorldPosition;

	/** Time the kite's simulation has advanced (s). */
	float SimTimeSeconds;

	float AirspeedCmS;
	float AngleOfAttackDeg;
	float TurnDeg;
	float CentredBarSeconds;
	float ParkClockDeg;
	float TurnRateRadS;
	float AppliedSteer = 0.0f;
	EKiteModel KiteModel = EKiteModel::Loop;
	FVector BarLeftEnd = FVector::ZeroVector;
	FVector BarRightEnd = FVector::ZeroVector;
	bool bHasBarEnds = false;
	bool bHasParkClock;
	bool bPlacementPending;
	bool bLoopHeld;
	bool bRiderAirborne = false;
	bool bLooping;
	float LoopSide;
	/** The rider was in the air at the last ComputeSteering: tells the step they left the water on. */
	bool bSteeredAirborne = false;
	/** The side (+1, -1) of a full bar held since the rider left the water, which does not loop in the air; 0 when none. */
	float AirHeldFullBarSide = 0.0f;
	/** The side (+1, -1) of the full bar being held in the air towards a loop, and for how long (s); 0 when none. */
	float AirFullBarSide = 0.0f;
	float AirFullBarSeconds = 0.0f;
	bool bCrashed;
	bool bLinesTaut;
	float CrashedSeconds;

	float LineTensionN;
	FVector LineForce;
	FKiteStepDebug LastStepDebug;
};
