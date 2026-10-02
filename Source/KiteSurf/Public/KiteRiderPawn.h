#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputActionValue.h"
#include "RiderCharacter.h"
#include "KiteRiderPawn.generated.h"

class UStaticMeshComponent;
class USkeletalMeshComponent;
class USpringArmComponent;
class UCameraComponent;
class UWindComponent;
class UBoardMovementComponent;
class UBoardWakeComponent;
class UKiteComponent;
class UInputMappingContext;
class UInputAction;
class UAudioComponent;
class USoundBase;

/** Volume and pitch for the three sound loops: wind in the ears, water under the board, lines under load. */
struct FRideAudioMix
{
	float WindVolume = 0.0f;
	float WindPitch = 1.0f;
	float WaterVolume = 0.0f;
	float WaterPitch = 1.0f;
	float LineVolume = 0.0f;
	float LinePitch = 1.0f;
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
	UWindComponent* GetWind() const { return Wind.Get(); }
	USkeletalMeshComponent* GetRiderMesh() const { return RiderMesh.Get(); }
	UStaticMeshComponent* GetControlBarMesh() const { return ControlBarMesh.Get(); }

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

	/** How loud and at what pitch each loop should play for what the rider is doing. Volumes 0..1, pitch 1 = as recorded. */
	static FRideAudioMix ComputeAudioMix(float ApparentWindKnots, float BoardSpeedKnots, bool bOnWater, float LineTensionN);

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

	UStaticMeshComponent* GetRiderStaticMesh() const { return RiderStaticMesh.Get(); }

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

	/** Lean away from the kite at full line load (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider")
	float RiderMaxLeanDeg;

	/** Extra lean back while floating in the water (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider")
	float RiderFloatLeanDeg;

	/** Most the rider hangs back from the harness in the air, with the kite low and pulling (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider")
	float RiderAirHangLeanDeg;

	/** How long the rider rides with their back to the kite before sliding the board round to face it (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider")
	float RiderSwitchDelaySeconds;

	/** How fast the rider comes round when they slide the board to face the kite (deg/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider")
	float RiderSwitchTurnRateDeg;

	/** The harness hook on the rider's body: forward, right, up from the feet (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider")
	FVector HarnessHookOffsetCm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UKiteComponent> Kite;

protected:
	virtual void BeginPlay() override;

	// Components
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BoardMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkeletalMeshComponent> RiderMesh;

	/** The posed riders (Santa, wetsuit); the robot uses the skeletal RiderMesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> RiderStaticMesh;

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

	// Sound: loops that play all the time and are faded and pitched by UpdateAudioModulation
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<UAudioComponent> WindLoopComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<UAudioComponent> WaterLoopComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<UAudioComponent> LineLoopComponent;

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
	void OnJumpTriggered(const FInputActionValue& Value);
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
