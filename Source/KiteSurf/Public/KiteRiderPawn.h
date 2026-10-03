#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputActionValue.h"
#include "RiderCharacter.h"
#include "KiteMotionBar.h"
#include "RiderRig.h"
#include "KiteRiderPawn.generated.h"

class UStaticMeshComponent;
class USkeletalMeshComponent;
class USpringArmComponent;
class UCameraComponent;
class UWindComponent;
class UBoardMovementComponent;
class UBoardWakeComponent;
class UWindStreakComponent;
class UKiteComponent;
class UTrickTrackerComponent;
class UInputMappingContext;
class UInputAction;
class UAudioComponent;
class USoundBase;

/** What the rider is doing, as far as it can be heard. */
struct FRideAudioState
{
	/** Wind past the rider's ears: true wind less their own motion (knots). */
	float ApparentWindKnots = 0.0f;
	float BoardSpeedKnots = 0.0f;
	bool bOnWater = true;
	float LineTensionN = 0.0f;
	/** Air over the kite (m/s): about the wind when it is parked, several times that through a loop. */
	float KiteAirspeedMS = 0.0f;
	/** The canopy has no load in it: slack lines, a stall, or the bar right out. 0..1. */
	float KiteLuff = 0.0f;
	/** How hard the edge is driven: carving or a loaded crouch. 0..1. */
	float EdgeEffort = 0.0f;
	bool bAirborne = false;
};

/** Volume and pitch for each sound loop, and how much of the music's second layer to play. */
struct FRideAudioMix
{
	float WindVolume = 0.0f;
	float WindPitch = 1.0f;
	float WaterVolume = 0.0f;
	float WaterPitch = 1.0f;
	float LineVolume = 0.0f;
	float LinePitch = 1.0f;
	/** Spray off a hard edge. */
	float SprayVolume = 0.0f;
	/** The kite moving through the air. */
	float KiteVolume = 0.0f;
	float KitePitch = 1.0f;
	/** A luffing canopy flapping. */
	float FlutterVolume = 0.0f;
	/** The music's in-the-air layer, 0..1 of the music volume. */
	float AirMusic = 0.0f;
};

/** Something that happened to the rider that has its own sound. */
enum class ERideSound : uint8
{
	KiteCrash,
	Relaunch,
	Aground,
	Shark
};

UCLASS()
class KITESURF_API AKiteRiderPawn : public APawn
{
	GENERATED_BODY()

public:
	AKiteRiderPawn();

	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	/**
	 * One fixed step of the whole rig, in this order: the kite with the rider where they are, the
	 * line force to the board, the board. Tick runs as many of these as the frame holds.
	 */
	UFUNCTION(BlueprintCallable, Category = "Simulation")
	void StepSimulation(float StepSeconds);

	/** Time the simulation has advanced (s). */
	UFUNCTION(BlueprintCallable, Category = "Simulation")
	float GetSimTimeSeconds() const { return SimTimeSeconds; }

	/** How many fixed steps the last frame ran. */
	int32 GetLastFrameSimSteps() const { return LastFrameSimSteps; }

	/** The physics debug level this pawn draws at: the kite.Physics.Debug console variable, or at least 1 when the kite's bDrawDebug is set. 0 in Shipping. */
	int32 GetPhysicsDebugLevel() const;

	/** The fixed step the kite, lines and board are simulated with (s). The same ride at any frame rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation", meta = (ClampMin = "0.0001"))
	float SimStepSeconds;

	/** Frame time above this is clamped (s), so a hitch slows the simulation down instead of blowing it up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation", meta = (ClampMin = "0.001"))
	float MaxFrameSeconds;

	/** Most fixed steps one frame will run; the rest of that frame's time is dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation", meta = (ClampMin = "1"))
	int32 MaxSimStepsPerFrame;

	/** Draw the rider and kite between the last two simulation states, so motion is smooth however the frame rate divides the step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	bool bInterpolateRendering;

	/** Step the kite and board from Tick. Off, they stay where they are and Tick only poses the rider, camera and sound: for tests that place the rider by hand. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	bool bStepSimulation;

	// Public API
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SteerKite(float Axis /* -1..1 */);

	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SheetKite(float Amount /* 0..1 */);

	UFUNCTION(BlueprintCallable, Category = "Board")
	void EdgeBoard(float Axis /* -1..1 */);

	UFUNCTION(BlueprintCallable, Category = "Board")
	bool Jump();

	UFUNCTION(BlueprintCallable, Category = "Kite")
	FVector GetBoardVelocity() const;

	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetKiteAzimuthDeg() const;

	UInputMappingContext* GetDefaultMappingContext() const { return DefaultMappingContext.Get(); }
	UInputAction* GetSteerAction() const { return SteerAction.Get(); }
	UInputAction* GetSheetAction() const { return SheetAction.Get(); }
	UInputAction* GetEdgeAction() const { return EdgeAction.Get(); }
	UInputAction* GetWeightShiftAction() const { return WeightShiftAction.Get(); }
	UInputAction* GetJumpAction() const { return JumpAction.Get(); }
	UInputAction* GetPauseAction() const { return PauseAction.Get(); }

	UKiteComponent* GetKite() const { return Kite.Get(); }
	UBoardMovementComponent* GetBoardMovement() const { return BoardMovement.Get(); }
	UBoardWakeComponent* GetWake() const { return Wake.Get(); }
	UWindStreakComponent* GetWindStreaks() const { return WindStreaks.Get(); }
	UWindComponent* GetWind() const { return Wind.Get(); }
	/** Names, grades and scores the rider's jumps; stepped after the board in StepSimulation. */
	UTrickTrackerComponent* GetTrickTracker() const { return TrickTracker.Get(); }
	UStaticMeshComponent* GetControlBarMesh() const { return ControlBarMesh.Get(); }

	/**
	 * The board that is drawn. The root BoardMesh is the physics body (it sweeps and collides) and
	 * is not rendered; this child shows the board and the rider's feet are in its straps. It follows
	 * the root exactly unless SetBoardVisualWorldRotation has turned it.
	 */
	UStaticMeshComponent* GetBoardVisual() const { return BoardVisual.Get(); }

	/**
	 * Turns the drawn board to this world rotation, whatever the root does, until
	 * ClearBoardVisualOverride. Its location still follows the root. For the rider attitude in the
	 * air, and later a board held in the hand. The physics body is not turned.
	 */
	void SetBoardVisualWorldRotation(const FQuat& WorldRotation);

	/** Puts the drawn board back on the root. */
	void ClearBoardVisualOverride();

	/** True while SetBoardVisualWorldRotation is turning the drawn board. */
	bool HasBoardVisualOverride() const { return bBoardVisualOverride; }

	/** World yaw the rider's body faces (deg). Always square across the board: the feet are in the straps. */
	UFUNCTION(BlueprintCallable, Category = "Rider")
	float GetRiderFacingYawDeg() const { return RiderFacingYawDeg; }

	/** World yaw the rider's body is shown facing: the stance yaw, plus any slide round still in progress. */
	UFUNCTION(BlueprintCallable, Category = "Rider")
	float GetRiderBodyYawDeg() const { return FRotator::NormalizeAxis(RiderFacingYawDeg + RiderTurnOffsetDeg); }

	/** Where the lines pull on the rider: the harness hook at the front of their waist. */
	UFUNCTION(BlueprintCallable, Category = "Rider")
	FVector GetHarnessHookWorldPosition() const { return HarnessHookPosition; }

	/** +1 when the rider faces the board's right rail, -1 when they face its left. */
	UFUNCTION(BlueprintCallable, Category = "Rider")
	float GetRiderStanceSide() const { return RiderStanceSide; }

	/** Which rail (+1 right, -1 left) a rider on a board at BoardYawDeg faces to look closest to PreferredFacingYawDeg. */
	static float ChooseStanceSide(float BoardYawDeg, float PreferredFacingYawDeg);

	/** Holds or lets go of the loaded crouch (the jump button held down). */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void SetLoadHeld(bool bHeld);

	/** The jump button let go: pops with whatever load has built up, then stands the rider up. True if they left the water. */
	UFUNCTION(BlueprintCallable, Category = "Input")
	bool ReleaseLoadAndPop();

	/** Brief controller vibration on the pop, landings, crashes, the kite hitting the water and a hard yank on the lines. */
	UFUNCTION(BlueprintCallable, Category = "Input|Haptics")
	void SetHapticsEnabled(bool bEnabled) { bHapticsEnabled = bEnabled; }

	UFUNCTION(BlueprintPure, Category = "Input|Haptics")
	bool AreHapticsEnabled() const { return bHapticsEnabled; }

	/** One short buzz: strength 0..1, for this long, on the heavy motors (a thump) or the light ones (a tick). Does nothing when haptics are off. */
	UFUNCTION(BlueprintCallable, Category = "Input|Haptics")
	void PlayHaptic(float Intensity, float DurationSeconds, bool bHeavy);

	/** How hard and how long a landing of this many g buzzes. */
	static void GetLandingHaptic(float LandingG, float& OutIntensity, float& OutDurationSeconds);

	/** Watches the line tension for a sudden hard pull (a loop's yank) and buzzes once for it. Tick calls this. */
	void UpdateTensionHaptic(float LineTensionN, float DeltaTime);

	/** How many buzzes have been asked for, and the last one, for tests. */
	int32 GetHapticCount() const { return HapticCount; }
	float GetLastHapticIntensity() const { return LastHapticIntensity; }
	float GetLastHapticDuration() const { return LastHapticDuration; }

	/** Line tension above which the lines' pull is felt as a yank (N). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Haptics")
	float HapticYankTensionN = 2200.0f;

	/**
	 * Uses the controller's motion sensors as the bar: tilt it like a bar to steer, tip its top
	 * towards you to pull the bar in. While it is on and a controller with sensors is found, the
	 * right stick, triggers and bar keys no longer move the bar; without one they carry on working.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input|Motion")
	void SetMotionBarEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Input|Motion")
	bool IsMotionBarEnabled() const { return bMotionBarEnabled; }

	/** True while the bar is actually following a controller's motion sensors. */
	UFUNCTION(BlueprintPure, Category = "Input|Motion")
	bool IsMotionBarActive() const { return bMotionBarActive; }

	/** Takes the way the controller is held now as "bar level, where it is". Done when the motion bar is switched on and on reset. */
	UFUNCTION(BlueprintCallable, Category = "Input|Motion")
	void RecentreMotionBar();

	/** The controller being read, for the settings screen; empty if none. */
	UFUNCTION(BlueprintPure, Category = "Input|Motion")
	FString GetMotionDeviceName() const;

	/** How the controller is being held, as the motion bar sees it: roll right and pitch towards the player (deg). */
	FVector2D GetMotionTiltDeg() const { return FVector2D(MotionFilter.GetRollDeg(), MotionFilter.GetPitchDeg()); }

	/** The last reading taken from the controller. */
	const FKiteMotionSample& GetLastMotionSample() const { return LastMotionSample; }

	/** Where motion readings come from. Tests put a scripted controller here; otherwise it is the platform's. */
	void SetMotionSource(TSharedPtr<IKiteMotionSource> InSource);

	/** How tilt becomes steering and bar position. */
	FMotionBarMapping MotionBarMapping;

	/** How loud and at what pitch each loop should play for what the rider is doing. Volumes 0..1, pitch 1 = as recorded. */
	static FRideAudioMix ComputeAudioMix(const FRideAudioState& State);

	/** The same for a rider with a parked kite and no edge: wind, water and lines only. */
	static FRideAudioMix ComputeAudioMix(float ApparentWindKnots, float BoardSpeedKnots, bool bOnWater, float LineTensionN);

	/** Plays the sound for something that happened. Running aground and a shark replace the splash of the crash they cause. */
	void PlayRideSound(ERideSound Sound);

	/** How many ride sounds have been asked for, and the last one, for tests. */
	int32 GetRideSoundCount() const { return RideSoundCount; }
	ERideSound GetLastRideSound() const { return LastRideSound; }

	/** Music volume, 0..1: the ride's music follows it at once. */
	UFUNCTION(BlueprintCallable, Category = "Audio")
	void SetMusicVolume(float Volume);

	UFUNCTION(BlueprintPure, Category = "Audio")
	float GetMusicVolume() const { return MusicVolume; }

	/** Ambient volume, 0..1: scales the wind, water, spray, line, kite and flutter loops. */
	UFUNCTION(BlueprintCallable, Category = "Audio")
	void SetAmbientVolume(float Volume);

	UFUNCTION(BlueprintPure, Category = "Audio")
	float GetAmbientVolume() const { return AmbientVolume; }

	/** Effects volume, 0..1: scales the one-shots (pop, landing, crashes, reset and the rest). */
	UFUNCTION(BlueprintCallable, Category = "Audio")
	void SetEffectsVolume(float Volume);

	UFUNCTION(BlueprintPure, Category = "Audio")
	float GetEffectsVolume() const { return EffectsVolume; }

	/** The volume the last one-shot was played at, after the effects volume, for tests. */
	float GetLastOneShotVolume() const { return LastOneShotVolume; }

	UAudioComponent* GetMusicBase() const { return MusicBaseComponent.Get(); }
	UAudioComponent* GetMusicAir() const { return MusicAirComponent.Get(); }
	UAudioComponent* GetSprayLoop() const { return SprayLoopComponent.Get(); }
	UAudioComponent* GetKiteLoop() const { return KiteLoopComponent.Get(); }
	UAudioComponent* GetFlutterLoop() const { return FlutterLoopComponent.Get(); }

	/** The mix the loops are playing at now. */
	const FRideAudioMix& GetAudioMix() const { return AudioMix; }

	UAudioComponent* GetWindLoop() const { return WindLoopComponent.Get(); }
	UAudioComponent* GetWaterLoop() const { return WaterLoopComponent.Get(); }
	UAudioComponent* GetLineLoop() const { return LineLoopComponent.Get(); }
	USoundBase* GetPopSound() const { return PopSound.Get(); }
	USoundBase* GetLandingSound() const { return LandingSound.Get(); }
	USoundBase* GetCrashSound() const { return CrashSound.Get(); }
	USoundBase* GetResetSound() const { return ResetSound.Get(); }

	UFUNCTION(BlueprintCallable, Category = "Input")
	float GetCurrentSteerInput() const { return CurrentSteerInput; }

	UFUNCTION(BlueprintCallable, Category = "Input")
	float GetCurrentSheetInput() const { return CurrentSheetInput; }

	/** Sets every held input at once, as the keys or sticks would. Used by the kitesurf.Input console command to script smoke runs. */
	void ApplyScriptedInput(float Steer, float SheetRate, float Carve, float WeightShift, bool bLoop);

	/** Shows the chosen rider on the board. */
	UFUNCTION(BlueprintCallable, Category = "Rider")
	void SetRiderCharacter(ERiderCharacter InCharacter);

	UFUNCTION(BlueprintCallable, Category = "Rider")
	ERiderCharacter GetRiderCharacter() const { return RiderCharacter; }

	/** The jointed rider's torso (pelvis to head). */
	UStaticMeshComponent* GetRiderStaticMesh() const { return RiderTorso.Get(); }

	/** The jointed rider's limb parts: thigh, shin, upper arm and forearm for the left side, then the same for the right. */
	const TArray<TObjectPtr<UStaticMeshComponent>>& GetRiderLimbs() const { return RiderLimbs; }

	/** How the jointed rider is posed now: where the pelvis, knees, feet, elbows and hands are. */
	const FRiderRigPose& GetRiderRigPose() const { return RiderPose; }

	/** Hold the bar moving in (+) or out (-), -1..1; 0 leaves it where it is. */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void SetSheetRateInput(float Axis);

	/** Sheet in (+) / out (-) input currently held, -1..1. The bar position itself is GetCurrentSheetInput(). */
	UFUNCTION(BlueprintCallable, Category = "Input")
	float GetSheetRateInput() const { return SheetRateInput; }

	/** Bar steering per unit of mouse travel while the right mouse button is held. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite")
	float MouseSteerSensitivity;

	/** Bar sheeting per unit of mouse travel while the right mouse button is held. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite")
	float MouseSheetSensitivity;

	/** Bar travel per second at full sheet input (0..1 range). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite")
	float SheetRatePerSec;

	// Camera framing: the view sits behind the rider and turns far enough towards the kite to keep it on screen.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraArmLengthCm;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraBoomPitchDeg;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraFOVDeg;

	/** Largest horizontal angle between the view direction and the kite (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraMaxKiteYawOffsetDeg;

	/** How far above the centre of the view the kite is allowed to sit before the camera tilts up (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraKiteHeadroomDeg;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraMinLookPitchDeg;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraMaxLookPitchDeg;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraTurnSpeed;

	/** Height of the camera boom's pivot above the board (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraPivotHeightCm;

	/**
	 * In the air the camera looks along the horizontal velocity, not the board, so spins do not
	 * swing it. Slower than this (cm/s) the direction means little and the last heading is held.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0"))
	float CameraAirMinSpeedCmS;

	/**
	 * How long the boom pivot takes to go from riding on the board's tilt (on the water) to sitting
	 * straight above the board whatever it does (in the air), and back (s).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0"))
	float CameraPivotLevelSeconds;

	/** Lean away from the kite at full line load (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider")
	float RiderMaxLeanDeg;

	/** Extra lean back while floating in the water (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider")
	float RiderFloatLeanDeg;

	/** Extra lean away from the kite in a full loaded crouch (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider")
	float RiderLoadLeanDeg;

	/** Most the rider hangs back from the harness in the air, with the kite low and pulling (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider")
	float RiderAirHangLeanDeg;

	/** How long the rider rides with their back to the kite before sliding the board round to face it (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider")
	float RiderSwitchDelaySeconds;

	/** How fast the rider comes round when they slide the board to face the kite (deg/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider")
	float RiderSwitchTurnRateDeg;

	/** The harness hook on the rider's body: forward, right, up from the pelvis (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider")
	FVector HarnessHookOffsetCm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UKiteComponent> Kite;

protected:
	virtual void BeginPlay() override;

	// Components
	/** The root and the physics body: swept by UBoardMovementComponent and collides, but is not rendered (BoardVisual is). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BoardMesh;

	/** The board that is drawn: a child of BoardMesh with no collision. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BoardVisual;

	/** The jointed riders (Santa, wetsuit): a torso and eight limb parts, posed every frame by RiderRig. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> RiderTorso;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TArray<TObjectPtr<UStaticMeshComponent>> RiderLimbs;

	FRiderRigPose RiderPose;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rider")
	ERiderCharacter RiderCharacter;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ControlBarMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UWindComponent> Wind;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoardMovementComponent> BoardMovement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoardWakeComponent> Wake;

	/** Wind lines on the water round the rider: how the wind's direction is read off the sea. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UWindStreakComponent> WindStreaks;

	/** Follows the jumps by polling the board and the kite: jump records, trick names and scores. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UTrickTrackerComponent> TrickTracker;

	// Sound: loops that play all the time and are faded and pitched by UpdateAudioModulation
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<UAudioComponent> WindLoopComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<UAudioComponent> WaterLoopComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<UAudioComponent> LineLoopComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<UAudioComponent> SprayLoopComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<UAudioComponent> KiteLoopComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<UAudioComponent> FlutterLoopComponent;

	/** The ride's music: two loops of the same length played in step. The second comes in while the rider is in the air. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<UAudioComponent> MusicBaseComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<UAudioComponent> MusicAirComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> KiteCrashSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> RelaunchSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> AgroundSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> SharkSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> PopSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> LandingSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> CrashSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> ResetSound;

	// Enhanced Input
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> SteerAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> SheetAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> EdgeAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> WeightShiftAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> PauseAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ResetAction;

public:
	UFUNCTION(BlueprintCallable, Category = "Gameplay")
	void ResetRider();

	UFUNCTION(BlueprintCallable, Category = "UI")
	void TogglePause();

private:
	void OnSteerTriggered(const FInputActionValue& Value);
	void OnSheetTriggered(const FInputActionValue& Value);
	void OnEdgeTriggered(const FInputActionValue& Value);
	void OnWeightShiftTriggered(const FInputActionValue& Value);
	void UpdateMouseBar();
	void OnJumpPressed(const FInputActionValue& Value);
	void OnJumpReleased(const FInputActionValue& Value);
	void OnPauseTriggered(const FInputActionValue& Value);
	void OnResetTriggered(const FInputActionValue& Value);

	UFUNCTION()
	void HandleBoardCrash(float Intensity);

	UFUNCTION()
	void HandleBoardReset();

	UFUNCTION()
	void HandleBoardLanding(float LandingG);

	void UpdateAudioModulation(float DeltaTime);

	/** kite.Physics.Debug 1: forces, winds and numbers at the kite and the rider, drawn for one frame. */
	void DrawPhysicsDebug() const;

	/** kite.Physics.Debug 2: one CSV line per fixed step to LogKiteSurf, so a ride can be plotted. */
	void LogPhysicsTelemetry();
	bool bLoggedTelemetryHeader = false;

	FRideAudioMix AudioMix;
	void PlayOneShot(USoundBase* Sound, float Volume, float Pitch = 1.0f);
	float MusicVolume = 0.6f;
	float AmbientVolume = 1.0f;
	float EffectsVolume = 1.0f;
	float LastOneShotVolume = 0.0f;
	int32 RideSoundCount = 0;
	ERideSound LastRideSound = ERideSound::KiteCrash;
	/** Set by running aground or a shark: the crash that follows keeps quiet, as they have their own sound. */
	bool bSkipNextCrashSplash = false;

	UFUNCTION()
	void HandleKiteRelaunched();

	void UpdateMotionBar(float DeltaTime);
	TSharedPtr<IKiteMotionSource> MotionSource;
	FMotionBarFilter MotionFilter;
	FKiteMotionSample LastMotionSample;
	bool bMotionBarEnabled = false;
	bool bMotionBarActive = false;
	bool bMotionRecentrePending = false;

	UFUNCTION()
	void HandleKiteCrashed(FVector Location);
	bool bHapticsEnabled = true;
	int32 HapticCount = 0;
	float LastHapticIntensity = 0.0f;
	float LastHapticDuration = 0.0f;
	float YankCooldownSeconds = 0.0f;
	bool bAboveYankTension = false;
	void UpdateCamera(float DeltaTime);
	void UpdateRiderPose(float DeltaTime);

	/** True once the kite simulation has placed the kite at line length from the rider. */
	bool HasKitePosition() const;

	float CurrentSteerInput;
	float CurrentSheetInput;
	float SheetRateInput;
	float KeySteerInput;
	float MouseSteerInput;
	/** Set by ApplyScriptedInput: the bar goes straight to the kite wherever it is. */
	bool bScriptedRawSteer;
	FVector SmoothedKiteOffset;
	float CameraYawDeg;
	float CameraLookPitchDeg;
	/** The direction the camera looks along before the kite clamp: the board on the water, the flight in the air (deg). */
	float CameraHeadingYawDeg;
	/** 0: the boom pivot rides on the board's pitch and roll, as on the water. 1: it sits straight above the board, as in the air. */
	float CameraPivotAirBlend;
	bool bBoardVisualOverride;
	float RiderFacingYawDeg;
	float RiderStanceSide;
	/** Yaw still to come off while the rider slides round to face the kite (deg). */
	float RiderTurnOffsetDeg;
	float BackToKiteSeconds;
	FVector HarnessHookPosition;
	/** Time left in which a rider who has just got on the board picks the rail that faces the kite (s). */
	float StanceChoiceSecondsLeft;
	static constexpr float StanceChoiceWindowSeconds = 0.3f;
	/** The board's reset count when the stance was last chosen for a reset. */
	int32 SeenBoardResetCount;
	bool bViewInitialized;
	uint64 LastPauseToggleFrame;
	float KiteAzimuthDeg;
	FVector BoardVelocity;

	// Fixed-step driver state
	float SimAccumulatorSeconds;
	float SimTimeSeconds;
	int32 LastFrameSimSteps;
	bool bHasSimState;
	/** The simulation's own transform, before and after the last step. */
	FVector PrevSimLocation;
	FVector SimLocation;
	FQuat PrevSimRotation;
	FQuat SimRotation;
	/** Where the root was last drawn, so a teleport from outside the step loop can be told apart from our own interpolation. */
	FVector LastRenderLocation;
	FQuat LastRenderRotation;
};
