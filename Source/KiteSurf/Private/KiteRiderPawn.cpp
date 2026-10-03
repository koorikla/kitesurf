#include "KiteRiderPawn.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "WindComponent.h"
#include "BoardMovementComponent.h"
#include "BoardWakeComponent.h"
#include "WindStreakComponent.h"
#include "KiteComponent.h"
#include "Tricks/TrickTrackerComponent.h"
#include "Tricks/RiderAttitudeComponent.h"
#include "Tricks/RiderAxes.h"
#include "Tricks/BoardGrabPoints.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "KiteSurf.h"
#include "KiteSurfHUD.h"
#include "UI/KiteSurfGameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "KiteSurfUnits.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarKitePhysicsDebug(
		TEXT("kite.Physics.Debug"),
		0,
		TEXT("Physics debug view of the rider's kite and board.\n")
		TEXT(" 0: off\n")
		TEXT(" 1: draw winds, forces and numbers at the kite and the rider, and a gust bar\n")
		TEXT(" 2: as 1, and log one CSV line per fixed step to LogKiteSurf (kitecsv)"),
		ECVF_Cheat);

#if !UE_BUILD_SHIPPING
	// Debug arrows: 10 m/s of wind and 500 N of force are each drawn about 2 m long.
	constexpr float DebugCmPerMS = 20.0f;
	constexpr float DebugCmPerN = 0.4f;
	constexpr float DebugHeadingLengthCm = 300.0f;
	constexpr float DebugArrowHeadCm = 30.0f;
	constexpr float DebugLineThickness = 3.0f;
	// The gust bar beside the rider: gust factor 0.5 at its foot, 1.5 at its top.
	constexpr float DebugGustBarMin = 0.5f;
	constexpr float DebugGustBarMax = 1.5f;
	constexpr float DebugGustBarLengthCm = 200.0f;
	constexpr float DebugGustBarBaseHeightCm = 250.0f;
	constexpr float DebugGustBarSideOffsetCm = 150.0f;
	constexpr float DebugKiteTextHeightCm = 150.0f;
	constexpr float DebugRiderTextHeightCm = 300.0f;
	constexpr float DebugBoardForceHeightCm = 20.0f;
	// The board's normal, tilted by its heel, drawn this long from the board.
	constexpr float DebugHeelNormalLengthCm = 150.0f;
	// The water under the board: its five samples as points this size (px), the fitted plane's normal
	// this long from the centre sample.
	constexpr float DebugWaterSamplePointSize = 12.0f;
	constexpr float DebugWaterNormalLengthCm = 120.0f;

	const TCHAR* BoardStateName(EBoardState State)
	{
		switch (State)
		{
		case EBoardState::Displacement: return TEXT("displacement");
		case EBoardState::Planing: return TEXT("planing");
		case EBoardState::Airborne: return TEXT("airborne");
		case EBoardState::Landing: return TEXT("landing");
		default: return TEXT("?");
		}
	}

	void DrawDebugVector(const UWorld* World, const FVector& Start, const FVector& Vector, float CmPerUnit, const FColor& Color)
	{
		const FVector Scaled = Vector * CmPerUnit;
		if (Scaled.SizeSquared() > 1.0f)
		{
			DrawDebugDirectionalArrow(World, Start, Start + Scaled, DebugArrowHeadCm, Color, false, -1.0f, 0, DebugLineThickness);
		}
	}
#endif
}

AKiteRiderPawn::AKiteRiderPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	// BoardMesh root: the physics body. UBoardMovementComponent sweeps it, so it keeps the board's
	// mesh for its collision, but it is not drawn: BoardVisual is.
	BoardMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BoardMesh"));
	RootComponent = BoardMesh;
	BoardMesh->SetVisibility(false);
	BoardMesh->SetHiddenInGame(true);

	// BoardVisual: the board that is drawn. It sits on the root (relative identity) unless
	// SetBoardVisualWorldRotation turns it, so the board can roll and flip in a trick while the
	// physics body keeps its own orientation.
	BoardVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BoardVisual"));
	BoardVisual->SetupAttachment(RootComponent);
	BoardVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoardVisual->SetGenerateOverlapEvents(false);
	BoardVisual->SetCanEverAffectNavigation(false);

	// Setup default meshes and materials if available
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BoardMeshFinder(TEXT("/Game/Meshes/SM_KiteBoard"));
	if (BoardMeshFinder.Succeeded())
	{
		BoardMesh->SetStaticMesh(BoardMeshFinder.Object);
		BoardVisual->SetStaticMesh(BoardMeshFinder.Object);
	}

	// The jointed riders: a torso and eight limb parts placed in world space every frame by
	// UpdateRiderPose. Which rider's parts they show is set by SetRiderCharacter.
	auto MakeRiderPart = [this](const TCHAR* Name) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(RootComponent);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetUsingAbsoluteLocation(true);
		Part->SetUsingAbsoluteRotation(true);
		Part->SetUsingAbsoluteScale(true);
		return Part;
	};
	RiderTorso = MakeRiderPart(TEXT("RiderTorso"));
	static const TCHAR* LimbNames[] = { TEXT("RiderLeftThigh"), TEXT("RiderLeftShin"), TEXT("RiderLeftUpperArm"), TEXT("RiderLeftForearm"), TEXT("RiderRightThigh"), TEXT("RiderRightShin"), TEXT("RiderRightUpperArm"), TEXT("RiderRightForearm") };
	for (const TCHAR* LimbName : LimbNames)
	{
		RiderLimbs.Add(MakeRiderPart(LimbName));
	}

	// ControlBarMesh attached to BoardMesh, positioned in front of rider chest height
	ControlBarMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ControlBarMesh"));
	ControlBarMesh->SetupAttachment(RootComponent);
	ControlBarMesh->SetRelativeLocation(FVector(40.0f, 0.0f, 100.0f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> BarMeshFinder(TEXT("/Game/Meshes/SM_ControlBar"));
	if (BarMeshFinder.Succeeded())
	{
		ControlBarMesh->SetStaticMesh(BarMeshFinder.Object);
	}

	SheetRatePerSec = 2.5f; // the whole throw in 0.4 s, as fast as arms move a bar
	BarNeutralSheet = 0.5f;
	BarReturnRatePerSec = 1.5f; // middle from either end in a third of a second
	MouseSteerSensitivity = 0.02f;
	MouseSheetSensitivity = 0.01f;
	CameraArmLengthCm = 1000.0f;
	CameraBoomPitchDeg = -12.0f;
	CameraFOVDeg = 95.0f;
	CameraMaxKiteYawOffsetDeg = 30.0f;
	CameraKiteHeadroomDeg = 22.0f;
	CameraMinLookPitchDeg = -6.0f;
	CameraMaxLookPitchDeg = 10.0f;
	CameraMaxFramingPitchDeg = 55.0f;
	CameraMaxFOVDeg = 115.0f;
	CameraMaxArmLengthCm = 2000.0f;
	CameraFrameMarginDeg = 2.0f;
	CameraComfortFrameFraction = 0.8f;
	CameraComfortTopFraction = 0.6f;
	CameraKiteFrameRadiusCm = 200.0f;
	CameraZoomOutSpeed = 4.0f;
	CameraZoomInSpeed = 0.8f;
	CameraMinHeightAboveWaterCm = 150.0f;
	CameraTurnSpeed = 2.5f;
	RiderMaxLeanDeg = 22.0f;
	RiderFloatLeanDeg = 30.0f;
	RiderAirHangLeanDeg = 38.0f;
	RiderLoadLeanDeg = 20.0f;
	RiderHarnessLeanDeg = 15.0f;
	RiderSwitchDelaySeconds = 0.4f;
	RiderSwitchTurnRateDeg = 540.0f;
	HarnessHookOffsetCm = FVector(22.0f, 0.0f, 16.0f);
	// T3.5 landing stances; every value an estimate (docs/tricks/T3.md T3.5).
	ToesideHoldSeconds = 3.0f;
	BlindHoldSeconds = 2.0f;
	ToesideTorsoTwistDeg = 70.0f;
	TorsoTwistRateDegPerSec = 240.0f;

	// The rider stands across the board and turns with it, but stays upright and leans against the kite
	// rather than tilting with the deck, so the pose is set in world space (see UpdateRiderPose).
	ControlBarMesh->SetUsingAbsoluteLocation(true);
	ControlBarMesh->SetUsingAbsoluteRotation(true);

	// CameraBoom: pivots at chest height and is aimed in world space by UpdateCamera, so the
	// horizon stays level while the board pitches with the swell and rolls with the edge. In the
	// air UpdateCamera also keeps the pivot straight above the board and looks along the flight.
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraPivotHeightCm = 120.0f;
	CameraAirMinSpeedCmS = 200.0f;
	CameraPivotLevelSeconds = 0.25f;
	CameraBoom->SetRelativeLocation(FVector(0.0f, 0.0f, CameraPivotHeightCm));
	CameraBoom->TargetArmLength = CameraArmLengthCm;
	CameraBoom->SetRelativeRotation(FRotator(CameraBoomPitchDeg, 0.0f, 0.0f));
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 3.0f;
	// The lag gives a sense of speed, but at 30 kn and more it would leave the camera far behind
	// and, rising off a kicker, below the rider; UpdateCamera frames with the lag it predicts.
	CameraBoom->CameraLagMaxDistance = 250.0f;
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->SetUsingAbsoluteRotation(true);

	// FollowCamera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
	FollowCamera->SetFieldOfView(CameraFOVDeg);

	// Wind component
	Wind = CreateDefaultSubobject<UWindComponent>(TEXT("Wind"));

	// Board movement component
	BoardMovement = CreateDefaultSubobject<UBoardMovementComponent>(TEXT("BoardMovement"));
	BoardMovement->UpdatedComponent = RootComponent;

	// Kite component: aerodynamics producing the line force consumed by BoardMovement
	Kite = CreateDefaultSubobject<UKiteComponent>(TEXT("Kite"));

	// The pawn steps the kite and the board itself, at a fixed rate and in a fixed order (see
	// Tick), so neither ticks on its own. Their step functions stay callable for tests.
	Kite->PrimaryComponentTick.bCanEverTick = false;
	BoardMovement->PrimaryComponentTick.bCanEverTick = false;

	// Jump records and tricks: polls the board and the kite after each board step.
	TrickTracker = CreateDefaultSubobject<UTrickTrackerComponent>(TEXT("TrickTracker"));
	TrickTracker->SetSources(BoardMovement, Kite);
	TrickTracker->SetGrabSource(&GrabState);
	TrickTracker->SetBarSource(&Bar);

	// The rider's rotation in the air: stepped by StepSimulation before the board (it does not tick).
	RiderAttitude = CreateDefaultSubobject<URiderAttitudeComponent>(TEXT("RiderAttitude"));
	bUseRiderAttitude = true;
	bPreWindLatchesBoardInput = true;
	PreWindBuildSeconds = 0.5f;
	PreWindStickThreshold = 0.3f;
	AirRotationDeadzone = 0.15f;
	RotateReleaseGraceSeconds = 0.1f;
	GrabReachFraction = 0.92f;
	GrabMaxBoardPullCm = 60.0f;
	OneFootKickStanceCm = FVector(-58.0f, -16.0f, 24.0f);
	VerticalAccelFilterSeconds = 0.1f;
	RiderHandoverSeconds = 0.2f;

	// Unhooked riding (T3.1, docs/tricks/T3.md 1.2 and 1.3).
	UnhookedStopperSheet = 0.45f; // T3.1 PR 3: the lowest the plan allows, the most ballistic pop with the kite at 45 deg
	LowParkElevationDeg = 45.0f;
	UnhookedArmExtensionDefault = 0.7f;
	// Freestyle hands (T3.2, T3.3).
	RaleyArmExtension = 0.85f;
	FlipArmExtension = 0.15f;
	bTantrumBackHandOff = true;
	RegrabBeforeContactSeconds = 0.4f;
	bFlickAssist = true;
	LeashLengthCm = 160.0f;

	SimStepSeconds = 1.0f / 240.0f;
	MaxFrameSeconds = 0.1f;
	MaxSimStepsPerFrame = 32;
	bInterpolateRendering = true;
	bStepSimulation = true;

	// Foam trail and spray behind the board
	Wake = CreateDefaultSubobject<UBoardWakeComponent>(TEXT("Wake"));

	// Wind lines on the sea
	WindStreaks = CreateDefaultSubobject<UWindStreakComponent>(TEXT("WindStreaks"));

	// Sound: three loops that are always playing and are faded and pitched by what the rider
	// would hear (UpdateAudioModulation), and one-shots for the pop, landing, crash and reset.
	auto MakeLoop = [this](const TCHAR* ComponentName, const TCHAR* SoundPath) -> UAudioComponent*
	{
		UAudioComponent* Loop = CreateDefaultSubobject<UAudioComponent>(ComponentName);
		Loop->SetupAttachment(RootComponent);
		Loop->bAutoActivate = false;
		Loop->bAllowSpatialization = false; // heard from the rider's own ears
		Loop->VolumeMultiplier = 0.0f;
		ConstructorHelpers::FObjectFinder<USoundBase> Finder(SoundPath);
		if (Finder.Succeeded())
		{
			Loop->SetSound(Finder.Object);
		}
		return Loop;
	};
	WindLoopComponent = MakeLoop(TEXT("WindLoopComponent"), TEXT("/Game/Audio/SW_WindLoop"));
	WaterLoopComponent = MakeLoop(TEXT("WaterLoopComponent"), TEXT("/Game/Audio/SW_WaterLoop"));
	LineLoopComponent = MakeLoop(TEXT("LineLoopComponent"), TEXT("/Game/Audio/SW_LineLoop"));
	SprayLoopComponent = MakeLoop(TEXT("SprayLoopComponent"), TEXT("/Game/Audio/SW_SprayLoop"));
	KiteLoopComponent = MakeLoop(TEXT("KiteLoopComponent"), TEXT("/Game/Audio/SW_KiteLoop"));
	FlutterLoopComponent = MakeLoop(TEXT("FlutterLoopComponent"), TEXT("/Game/Audio/SW_FlutterLoop"));

	// Music: plays through a pause, and is not part of the world's sound.
	MusicBaseComponent = MakeLoop(TEXT("MusicBaseComponent"), TEXT("/Game/Audio/MU_RideBase"));
	MusicAirComponent = MakeLoop(TEXT("MusicAirComponent"), TEXT("/Game/Audio/MU_RideAir"));
	for (UAudioComponent* Music : { MusicBaseComponent.Get(), MusicAirComponent.Get() })
	{
		Music->bIsUISound = true;
	}

	auto FindSound = [](const TCHAR* SoundPath) -> USoundBase*
	{
		ConstructorHelpers::FObjectFinder<USoundBase> Finder(SoundPath);
		return Finder.Succeeded() ? Finder.Object : nullptr;
	};
	PopSound = FindSound(TEXT("/Game/Audio/SW_Pop"));
	LandingSound = FindSound(TEXT("/Game/Audio/SW_Landing"));
	CrashSound = FindSound(TEXT("/Game/Audio/SW_Crash"));
	ResetSound = FindSound(TEXT("/Game/Audio/SW_ResetCue"));
	StompSound = FindSound(TEXT("/Game/Audio/SW_Stomp"));
	KiteCrashSound = FindSound(TEXT("/Game/Audio/SW_KiteCrash"));
	RelaunchSound = FindSound(TEXT("/Game/Audio/SW_Relaunch"));
	AgroundSound = FindSound(TEXT("/Game/Audio/SW_Aground"));
	SharkSound = FindSound(TEXT("/Game/Audio/SW_Shark"));

	RiderCharacter = ERiderCharacter::Santa;
	SetRiderCharacter(RiderCharacter);

	CurrentSteerInput = 0.0f;
	CurrentSheetInput = 0.0f;
	SheetRateInput = 0.0f;
	bBarReturnsToMiddle = true;
	bPlayerSheetInput = false;
	PlayerSheetOffset = 0.0f;
	KeySteerInput = 0.0f;
	MouseSteerInput = 0.0f;
	bScriptedRawSteer = false;
	SmoothedKiteOffset = FVector::ZeroVector;
	CameraYawDeg = 0.0f;
	CameraLookPitchDeg = 0.0f;
	CameraHeadingYawDeg = 0.0f;
	CameraPivotAirBlend = 0.0f;
	CameraCurrentFOVDeg = CameraFOVDeg;
	CameraCurrentArmCm = CameraArmLengthCm;
	CameraCurrentBoomPitchDeg = CameraBoomPitchDeg;
	CameraLastPivotLocation = FVector::ZeroVector;
	bBoardVisualOverride = false;
	RiderFacingYawDeg = 0.0f;
	RiderStanceSide = 1.0f;
	RiderTurnOffsetDeg = 0.0f;
	BackToKiteSeconds = 0.0f;
	HarnessHookPosition = FVector::ZeroVector;
	SeenBoardResetCount = 0;
	StanceChoiceSecondsLeft = StanceChoiceWindowSeconds;
	bViewInitialized = false;
	LastPauseToggleFrame = MAX_uint64;
	KiteAzimuthDeg = 0.0f;
	BoardVelocity = FVector::ZeroVector;
	SimAccumulatorSeconds = 0.0f;
	SimTimeSeconds = 0.0f;
	LastFrameSimSteps = 0;
	bHasSimState = false;
	PrevSimLocation = FVector::ZeroVector;
	SimLocation = FVector::ZeroVector;
	PrevSimRotation = FQuat::Identity;
	SimRotation = FQuat::Identity;
	LastRenderLocation = FVector::ZeroVector;
	LastRenderRotation = FQuat::Identity;
}

void AKiteRiderPawn::BeginPlay()
{
	Super::BeginPlay();

	// The boom places the camera after UpdateCamera has aimed it this frame, so the framing it
	// worked out is the one drawn.
	if (CameraBoom)
	{
		CameraBoom->AddTickPrerequisiteActor(this);
	}

	if (const UWorld* World = GetWorld())
	{
		if (const UKiteSurfGameInstance* GI = Cast<UKiteSurfGameInstance>(World->GetGameInstance()))
		{
			SetRiderCharacter(GI->RiderCharacter);
			SetMotionBarEnabled(GI->bMotionBar);
			SetMotionSheetMode(GI->MotionSheetMode);
			SetBarReturnsToMiddle(GI->bBarReturnsToMiddle);
			SetHapticsEnabled(GI->bHaptics);
			SetMusicVolume(GI->MusicVolume);
			SetAmbientVolume(GI->AmbientVolume);
			SetEffectsVolume(GI->EffectsVolume);
		}
	}

	if (BoardMovement)
	{
		BoardMovement->OnBoardCrash.AddDynamic(this, &AKiteRiderPawn::HandleBoardCrash);
		BoardMovement->OnBoardReset.AddDynamic(this, &AKiteRiderPawn::HandleBoardReset);
		BoardMovement->OnBoardLanding.AddDynamic(this, &AKiteRiderPawn::HandleBoardLanding);
		BoardMovement->OnBoardLandingVerdict.AddDynamic(this, &AKiteRiderPawn::HandleBoardLandingVerdict);
	}
	if (Kite)
	{
		Kite->OnKiteCrashed.AddDynamic(this, &AKiteRiderPawn::HandleKiteCrashed);
		Kite->OnKiteRelaunched.AddDynamic(this, &AKiteRiderPawn::HandleKiteRelaunched);
	}

	// The two music loops start on the same frame so that they stay in step.
	for (UAudioComponent* Loop : { WindLoopComponent.Get(), WaterLoopComponent.Get(), LineLoopComponent.Get(), SprayLoopComponent.Get(), KiteLoopComponent.Get(), FlutterLoopComponent.Get(), MusicBaseComponent.Get(), MusicAirComponent.Get() })
	{
		if (Loop && Loop->GetSound())
		{
			Loop->Play();
		}
	}

	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			if (DefaultMappingContext)
			{
				Subsystem->AddMappingContext(DefaultMappingContext, 0);
			}
		}
	}
}

void AKiteRiderPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (SteerAction)
		{
			EnhancedInputComponent->BindAction(SteerAction, ETriggerEvent::Triggered, this, &AKiteRiderPawn::OnSteerTriggered);
			EnhancedInputComponent->BindAction(SteerAction, ETriggerEvent::Completed, this, &AKiteRiderPawn::OnSteerTriggered);
		}
		if (SheetAction)
		{
			EnhancedInputComponent->BindAction(SheetAction, ETriggerEvent::Triggered, this, &AKiteRiderPawn::OnSheetTriggered);
			EnhancedInputComponent->BindAction(SheetAction, ETriggerEvent::Completed, this, &AKiteRiderPawn::OnSheetTriggered);
		}
		if (EdgeAction)
		{
			EnhancedInputComponent->BindAction(EdgeAction, ETriggerEvent::Triggered, this, &AKiteRiderPawn::OnEdgeTriggered);
			EnhancedInputComponent->BindAction(EdgeAction, ETriggerEvent::Completed, this, &AKiteRiderPawn::OnEdgeTriggered);
		}
		if (WeightShiftAction)
		{
			EnhancedInputComponent->BindAction(WeightShiftAction, ETriggerEvent::Triggered, this, &AKiteRiderPawn::OnWeightShiftTriggered);
			EnhancedInputComponent->BindAction(WeightShiftAction, ETriggerEvent::Completed, this, &AKiteRiderPawn::OnWeightShiftTriggered);
		}
		if (PauseAction)
		{
			EnhancedInputComponent->BindAction(PauseAction, ETriggerEvent::Started, this, &AKiteRiderPawn::OnPauseTriggered);
		}
		if (JumpAction)
		{
			// Held: crouch and load the edge. Released: pop.
			EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &AKiteRiderPawn::OnJumpPressed);
			EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &AKiteRiderPawn::OnJumpReleased);
		}
		if (ResetAction)
		{
			EnhancedInputComponent->BindAction(ResetAction, ETriggerEvent::Started, this, &AKiteRiderPawn::OnResetTriggered);
		}
		if (RecenterMotionAction)
		{
			EnhancedInputComponent->BindAction(RecenterMotionAction, ETriggerEvent::Started, this, &AKiteRiderPawn::OnRecenterMotionTriggered);
		}
		// Grabs and the one-footer (T2.1, T2.2): held, so Started and Completed.
		if (GrabFrontAction)
		{
			EnhancedInputComponent->BindAction(GrabFrontAction, ETriggerEvent::Started, this, &AKiteRiderPawn::OnGrabFrontPressed);
			EnhancedInputComponent->BindAction(GrabFrontAction, ETriggerEvent::Completed, this, &AKiteRiderPawn::OnGrabFrontReleased);
		}
		if (GrabBackAction)
		{
			EnhancedInputComponent->BindAction(GrabBackAction, ETriggerEvent::Started, this, &AKiteRiderPawn::OnGrabBackPressed);
			EnhancedInputComponent->BindAction(GrabBackAction, ETriggerEvent::Completed, this, &AKiteRiderPawn::OnGrabBackReleased);
		}
		if (OneFootAction)
		{
			EnhancedInputComponent->BindAction(OneFootAction, ETriggerEvent::Started, this, &AKiteRiderPawn::OnOneFootPressed);
			EnhancedInputComponent->BindAction(OneFootAction, ETriggerEvent::Completed, this, &AKiteRiderPawn::OnOneFootReleased);
		}
		// Unhooked riding (T3.1): presses, kept for the next fixed step.
		if (HookAction)
		{
			EnhancedInputComponent->BindAction(HookAction, ETriggerEvent::Started, this, &AKiteRiderPawn::OnHookPressed);
		}
		if (PassAction)
		{
			EnhancedInputComponent->BindAction(PassAction, ETriggerEvent::Started, this, &AKiteRiderPawn::OnPassPressed);
		}
		// The rotation modifier (batch A): held, so Started and Completed, like the pass and jump.
		if (RotateAction)
		{
			EnhancedInputComponent->BindAction(RotateAction, ETriggerEvent::Started, this, &AKiteRiderPawn::OnRotatePressed);
			EnhancedInputComponent->BindAction(RotateAction, ETriggerEvent::Completed, this, &AKiteRiderPawn::OnRotateReleased);
		}
	}

	// Fallback binding for standard Escape key in case Enhanced Input action is unassigned
	PlayerInputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AKiteRiderPawn::TogglePause);
	PlayerInputComponent->BindKey(EKeys::P, IE_Pressed, this, &AKiteRiderPawn::TogglePause);
	PlayerInputComponent->BindKey(EKeys::Gamepad_Special_Right, IE_Pressed, this, &AKiteRiderPawn::TogglePause);

	// Fallback binding for manual reset
	PlayerInputComponent->BindKey(EKeys::R, IE_Pressed, this, &AKiteRiderPawn::ResetRider);
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Right, IE_Pressed, this, &AKiteRiderPawn::ResetRider);
	PlayerInputComponent->BindKey(EKeys::Gamepad_Special_Left, IE_Pressed, this, &AKiteRiderPawn::ResetRider);
}

void AKiteRiderPawn::PlayHaptic(float Intensity, float DurationSeconds, bool bHeavy)
{
	if (!bHapticsEnabled || Intensity <= 0.0f || DurationSeconds <= 0.0f)
	{
		return;
	}
	++HapticCount;
	LastHapticIntensity = FMath::Clamp(Intensity, 0.0f, 1.0f);
	LastHapticDuration = DurationSeconds;
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		// A thump on the big motors, a tick on the small ones.
		PC->PlayDynamicForceFeedback(LastHapticIntensity, DurationSeconds, bHeavy, !bHeavy, bHeavy, !bHeavy);
	}
}

void AKiteRiderPawn::GetLandingHaptic(float LandingG, float& OutIntensity, float& OutDurationSeconds)
{
	// A soft touchdown is a light tap; five g and more is a full thump that lasts.
	const float Hardness = FMath::Clamp((LandingG - 1.0f) / 4.0f, 0.0f, 1.0f);
	OutIntensity = 0.3f + 0.7f * Hardness;
	OutDurationSeconds = 0.12f + 0.2f * Hardness;
}

void AKiteRiderPawn::UpdateTensionHaptic(float LineTensionN, float DeltaTime)
{
	YankCooldownSeconds = FMath::Max(YankCooldownSeconds - DeltaTime, 0.0f);
	const bool bAbove = LineTensionN >= HapticYankTensionN;
	// Once as the pull comes on, not for as long as it lasts.
	if (bAbove && !bAboveYankTension && YankCooldownSeconds <= 0.0f)
	{
		PlayHaptic(FMath::Clamp(0.35f + 0.5f * (LineTensionN - HapticYankTensionN) / 3000.0f, 0.35f, 0.85f), 0.12f, true);
		YankCooldownSeconds = 0.8f;
	}
	bAboveYankTension = bAbove;
}

void AKiteRiderPawn::HandleKiteCrashed(FVector Location)
{
	PlayRideSound(ERideSound::KiteCrash);
	PlayHaptic(0.6f, 0.25f, true);
}

void AKiteRiderPawn::HandleKiteRelaunched()
{
	PlayRideSound(ERideSound::Relaunch);
}

void AKiteRiderPawn::PlayOneShot(USoundBase* Sound, float Volume, float Pitch)
{
	LastOneShotVolume = Volume * EffectsVolume;
	if (Sound && LastOneShotVolume > 0.0f)
	{
		// Never quite the same twice.
		UGameplayStatics::PlaySound2D(this, Sound, LastOneShotVolume, Pitch * FMath::FRandRange(0.95f, 1.05f));
	}
}

void AKiteRiderPawn::PlayRideSound(ERideSound Sound)
{
	++RideSoundCount;
	LastRideSound = Sound;
	switch (Sound)
	{
	case ERideSound::KiteCrash:
		PlayOneShot(KiteCrashSound, 0.7f);
		break;
	case ERideSound::Relaunch:
		PlayOneShot(RelaunchSound, 0.6f);
		break;
	case ERideSound::Aground:
		PlayOneShot(AgroundSound, 0.9f);
		bSkipNextCrashSplash = true;
		break;
	case ERideSound::Shark:
		PlayOneShot(SharkSound, 0.9f);
		bSkipNextCrashSplash = true;
		break;
	}
}

void AKiteRiderPawn::SetMusicVolume(float Volume)
{
	MusicVolume = FMath::Clamp(Volume, 0.0f, 1.0f);
}

void AKiteRiderPawn::SetAmbientVolume(float Volume)
{
	AmbientVolume = FMath::Clamp(Volume, 0.0f, 1.0f);
}

void AKiteRiderPawn::SetEffectsVolume(float Volume)
{
	EffectsVolume = FMath::Clamp(Volume, 0.0f, 1.0f);
}

void AKiteRiderPawn::SetMotionBarEnabled(bool bEnabled)
{
	bMotionBarEnabled = bEnabled;
	if (bEnabled)
	{
		if (!MotionSource)
		{
			MotionSource = KiteMotionBar::CreatePlatformSource();
		}
		MotionFilter.Reset();
		bMotionRecentrePending = true;
	}
	else if (bMotionBarActive)
	{
		// Hand the bar back level, where it is.
		bMotionBarActive = false;
		PlayerSheetOffset = 0.0f;
		SteerKite(KeySteerInput + MouseSteerInput);
	}
}

void AKiteRiderPawn::RecentreMotionBar()
{
	bMotionRecentrePending = true;
}

void AKiteRiderPawn::RecentreMotionBarToMiddle()
{
	// The right stick is the bar while the motion bar is not following a controller: a stray click
	// of it then must not move anything.
	if (!bMotionBarActive)
	{
		return;
	}
	bMotionRecentrePending = true;
	bMotionRecentreToMiddle = true;
	++MotionRecentreButtonCount;
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (AKiteSurfHUD* HUD = Cast<AKiteSurfHUD>(PC->GetHUD()))
		{
			HUD->ShowNotice(TEXT("Controller recentred"));
		}
	}
}

void AKiteRiderPawn::OnRecenterMotionTriggered(const FInputActionValue& Value)
{
	RecentreMotionBarToMiddle();
}

void AKiteRiderPawn::SetMotionSheetMode(EMotionSheetMode InMode)
{
	if (MotionSheetMode == InMode)
	{
		return;
	}
	MotionSheetMode = InMode;
	// The new mode starts from the bar where it is and the pad as it is held now.
	MotionStroke.Reset();
	bMotionRecentrePending = true;
}

void AKiteRiderPawn::SetBarReturnsToMiddle(bool bEnabled)
{
	bBarReturnsToMiddle = bEnabled;
	if (!bEnabled)
	{
		// The bar stays where the spring had it.
		ReleasePlayerSheetSpring();
	}
}

FString AKiteRiderPawn::GetMotionDeviceName() const
{
	return MotionSource ? MotionSource->GetDeviceName() : FString();
}

void AKiteRiderPawn::SetMotionSource(TSharedPtr<IKiteMotionSource> InSource)
{
	MotionSource = InSource;
	MotionFilter.Reset();
	bMotionRecentrePending = true;
}

void AKiteRiderPawn::UpdateMotionBar(float DeltaTime)
{
	FKiteMotionSample Sample;
	const bool bHaveReading = bMotionBarEnabled && MotionSource && MotionSource->Poll(Sample);
	if (!bHaveReading)
	{
		if (bMotionBarActive)
		{
			// The controller went away: the stick and keys have the bar again.
			bMotionBarActive = false;
			MotionFilter.Reset();
			PlayerSheetOffset = 0.0f;
			SteerKite(KeySteerInput + MouseSteerInput);
		}
		return;
	}

	LastMotionSample = Sample;
	MotionFilter.Update(Sample, DeltaTime);
	if (!MotionFilter.IsInitialized())
	{
		return;
	}
	if (bMotionRecentrePending || !bMotionBarActive)
	{
		// However the pad is being held now is "bar level": no jump in steering, and no jump in power
		// unless the recentre button asked for the bar in the middle.
		MotionBarMapping.Calibrate(MotionFilter.GetRollDeg(), MotionFilter.GetPitchDeg(), bMotionRecentreToMiddle ? BarNeutralSheet : CurrentSheetInput);
		MotionStroke.Reset();
		PlayerSheetOffset = 0.0f;
		bMotionRecentrePending = false;
		bMotionRecentreToMiddle = false;
	}
	bMotionBarActive = true;

	if (MotionSheetMode == EMotionSheetMode::Move)
	{
		// The travel stops at the ends of the bar's throw, and with the bar returning to the middle it
		// creeps back there, so drift does not build up between recentres.
		MotionStroke.MinDisplacementCm = MotionBarMapping.GetStrokeMinCm();
		MotionStroke.MaxDisplacementCm = MotionBarMapping.GetStrokeMaxCm();
		MotionStroke.RelaxSeconds = bBarReturnsToMiddle ? MoveRelaxSeconds : 0.0f;
		MotionStroke.RelaxTargetCm = (BarNeutralSheet - MotionBarMapping.SheetAtNeutral) * MotionBarMapping.MoveSheetFullStrokeCm;
		MotionStroke.Update(Sample, MotionFilter.GetUp(), DeltaTime);
	}

	// The right stick is taken out of the steering: the keys and the mouse still add to the tilt.
	float StickSteer = 0.0f;
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		StickSteer = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX);
	}
	SteerKite(KeySteerInput - StickSteer + MouseSteerInput + MotionBarMapping.GetSteer(MotionFilter.GetRollDeg()));
	// The bar's position is set by UpdateBarSheet, which adds the keys' trim.
}

float AKiteRiderPawn::GetMotionSheet() const
{
	if (MotionSheetMode == EMotionSheetMode::Move)
	{
		return MotionBarMapping.GetSheetFromStroke(MotionStroke.GetDisplacementCm());
	}
	return MotionBarMapping.GetSheet(MotionFilter.GetPitchDeg());
}

float AKiteRiderPawn::GetSpringSheetTarget(float Input) const
{
	const float Neutral = FMath::Clamp(BarNeutralSheet, 0.0f, 1.0f);
	const float Clamped = FMath::Clamp(Input, -1.0f, 1.0f);
	return Clamped >= 0.0f ? Neutral + Clamped * (1.0f - Neutral) : Neutral + Clamped * Neutral;
}

void AKiteRiderPawn::ReleasePlayerSheetSpring()
{
	if (!FMath::IsNearlyZero(PlayerSheetOffset))
	{
		// The keys' trim on the motion bar becomes part of where the bar sits.
		MotionBarMapping.SheetAtNeutral = FMath::Clamp(MotionBarMapping.SheetAtNeutral + PlayerSheetOffset, 0.0f, 1.0f);
		PlayerSheetOffset = 0.0f;
	}
	bPlayerSheetInput = false;
}

void AKiteRiderPawn::UpdateBarSheet(float DeltaTime)
{
	const bool bSpring = bBarReturnsToMiddle && bPlayerSheetInput;
	const bool bHeld = FMath::Abs(SheetRateInput) > KINDA_SMALL_NUMBER;
	// Out at the bar's own rate while held, back to the middle at the return rate once let go.
	const float SpringRate = bHeld ? SheetRatePerSec : BarReturnRatePerSec;

	if (bMotionBarActive)
	{
		if (bSpring)
		{
			// The keys, stick and triggers trim the controller's bar while held and spring back when let
			// go: full input is fully in or out from wherever the controller has the bar.
			const float Base = GetMotionSheet();
			const float Target = SheetRateInput >= 0.0f ? SheetRateInput * (1.0f - Base) : SheetRateInput * Base;
			PlayerSheetOffset = FMath::FInterpConstantTo(PlayerSheetOffset, Target, DeltaTime, SpringRate);
		}
		else if (!FMath::IsNearlyZero(SheetRateInput))
		{
			// Scripted, or with the spring off: the input moves where the bar sits for the controller.
			MotionBarMapping.SheetAtNeutral = FMath::Clamp(MotionBarMapping.SheetAtNeutral + SheetRateInput * SheetRatePerSec * DeltaTime, 0.0f, 1.0f);
		}
		ApplySheet(GetMotionSheet() + PlayerSheetOffset);
		return;
	}

	if (bSpring)
	{
		ApplySheet(FMath::FInterpConstantTo(CurrentSheetInput, GetSpringSheetTarget(SheetRateInput), DeltaTime, SpringRate));
	}
	else if (!FMath::IsNearlyZero(SheetRateInput))
	{
		// The bar stays where it is put: the input moves it in or out instead of setting it.
		ApplySheet(CurrentSheetInput + SheetRateInput * SheetRatePerSec * DeltaTime);
	}
}

void AKiteRiderPawn::OnSteerTriggered(const FInputActionValue& Value)
{
	KeySteerInput = FMath::Clamp(Value.Get<float>(), -1.0f, 1.0f);
	if (!bMotionBarActive)
	{
		SteerKite(KeySteerInput + MouseSteerInput);
	}
}

void AKiteRiderPawn::ApplyScriptedInput(float Steer, float SheetRate, float Carve, float WeightShift, bool bLoop, FVector2D AirRotation, float Tuck)
{
	SetAirRotationInput(AirRotation);
	SetTuck(Tuck);
	KeySteerInput = FMath::Clamp(Steer, -1.0f, 1.0f);
	SteerKite(KeySteerInput);
	SetSheetRateInput(SheetRate);
	EdgeBoard(Carve);
	if (BoardMovement)
	{
		BoardMovement->SetWeightShift(WeightShift);
	}
	bScriptedRawSteer = bLoop;
}

void AKiteRiderPawn::SetPreWind(FVector2D Stick)
{
	bPlayerRiderInput = false; // the scripted path takes over
	PreWindStick = Stick.GetClampedToMaxSize(1.0f);
}

void AKiteRiderPawn::SetAirRotationInput(FVector2D Stick)
{
	bPlayerRiderInput = false;
	AirRotationStick = Stick.GetClampedToMaxSize(1.0f);
}

void AKiteRiderPawn::SetTrickInput(bool bGrabFront, bool bGrabBack, bool bOneFoot, FVector2D ZoneStick)
{
	bGrabFrontHeld = bGrabFront;
	bGrabBackHeld = bGrabBack;
	bOneFootHeld = bOneFoot;
	GrabZoneStick = ZoneStick.GetClampedToMaxSize(1.0f);
	bScriptedGrabZoneStick = true;
}

void AKiteRiderPawn::SetTuck(float Amount)
{
	bPlayerRiderInput = false;
	TuckInput = FMath::Clamp(Amount, 0.0f, 1.0f);
}

void AKiteRiderPawn::OnWeightShiftTriggered(const FInputActionValue& Value)
{
	PlayerRiderStick.Y = FMath::Clamp(Value.Get<float>(), -1.0f, 1.0f);
	bPlayerRiderInput = true;
	bScriptedGrabZoneStick = false;
	UpdateScreenBackSignLatch();
	RoutePlayerRiderInput();
}

float AKiteRiderPawn::ComputeScreenBackSign(const FVector& BodyFront, const FVector& CameraRight, float Fallback)
{
	const FVector Back = FVector(-BodyFront.X, -BodyFront.Y, 0.0f).GetSafeNormal();
	const FVector Right = FVector(CameraRight.X, CameraRight.Y, 0.0f).GetSafeNormal();
	constexpr float MinSideDot = 0.1f;
	const float D = static_cast<float>(Back | Right);
	if (Back.IsZero() || Right.IsZero() || FMath::Abs(D) < MinSideDot)
	{
		return Fallback >= 0.0f ? 1.0f : -1.0f;
	}
	return D > 0.0f ? 1.0f : -1.0f;
}

void AKiteRiderPawn::UpdateScreenBackSignLatch()
{
	if (!BoardMovement)
	{
		return;
	}
	const bool bAirborne = BoardMovement->GetBoardState() == EBoardState::Airborne;
	const bool bLoading = !bAirborne && BoardMovement->IsLoadHeld();
	// Latched as the load starts (the rider winds up looking at the screen as it is then), or at a
	// take-off without a load; a pop out of the load keeps the load's.
	const bool bLoadStarts = bLoading && !bBackSignWasLoading;
	const bool bUnloadedTakeoff = bAirborne && !bBackSignWasAirborne && !bBackSignWasLoading;
	if (bLoadStarts || bUnloadedTakeoff)
	{
		const FVector BodyFront = (bAirborne && RiderAttitude && RiderAttitude->IsSimulating())
			? RiderAttitude->GetBodyQuat().GetAxisX()
			: FRotator(0.0f, GetRiderBodyYawDeg(), 0.0f).Vector();
		// The chase camera's yaw, as UpdateCamera last set the boom: the view's right on the water.
		const FVector CameraRight = FRotationMatrix(FRotator(0.0f, CameraYawDeg, 0.0f)).GetUnitAxis(EAxis::Y);
		ScreenBackSign = ComputeScreenBackSign(BodyFront, CameraRight, ScreenBackSign);
	}
	bBackSignWasLoading = bLoading;
	bBackSignWasAirborne = bAirborne;
}

void AKiteRiderPawn::RoutePlayerRiderInput()
{
	if (!bPlayerRiderInput || !BoardMovement)
	{
		return;
	}
	const bool bAirborne = BoardMovement->GetBoardState() == EBoardState::Airborne;
	if (!RiderAttitude || !bUseRiderAttitude)
	{
		// Without the attitude the stick is the board everywhere, as before T1.2: carve input spins
		// the board in the air (AirSpinRate).
		EdgeBoard(PlayerRiderStick.X);
		BoardMovement->SetWeightShift(PlayerRiderStick.Y);
		PreWindStick = FVector2D::ZeroVector;
		AirRotationStick = FVector2D::ZeroVector;
		TuckInput = 0.0f;
		return;
	}

	const FVector2D Rotation = FVector2D(PlayerRiderStick.X * ScreenBackSign, PlayerRiderStick.Y).GetClampedToMaxSize(1.0f);
	if (bAirborne)
	{
		// The rotation stick and the tuck. The board keeps its last carve and weight shift until the
		// landing; the attitude flies it in the air. Batch A: IA_Rotate gates the rotation stick, so
		// a plain jump's board input does nothing to the attitude unless the modifier is held.
		AirRotationStick = bRotateHeld ? Rotation : FVector2D::ZeroVector;
		PreWindStick = FVector2D::ZeroVector;
		TuckInput = bPlayerTuckHeld ? 1.0f : 0.0f;
		// The same stick picks the grab zone while a grab button is held (StepRiderAttitude then
		// leaves the rotation alone), whatever IA_Rotate is doing: a grab button already commits to
		// picking a zone, not a rotation.
		if (!bScriptedGrabZoneStick)
		{
			GrabZoneStick = Rotation;
		}
		return;
	}

	AirRotationStick = FVector2D::ZeroVector;
	TuckInput = 0.0f;
	bPlayerTuckHeld = false;
	const bool bLoading = BoardMovement->IsLoadHeld();
	// Batch A: the stick reaches the pre-wind only with IA_Rotate held too. The latch that freezes
	// the board's carve and weight shift applies only while both are held, so the tail weight and
	// the carve a plain load is taught with (README.md, School B1/B2) keep working through it.
	const bool bRotateGate = bLoading && bRotateHeld;
	PreWindStick = bRotateGate ? Rotation : FVector2D::ZeroVector;
	if (!bRotateGate || !bPreWindLatchesBoardInput)
	{
		EdgeBoard(PlayerRiderStick.X);
		BoardMovement->SetWeightShift(PlayerRiderStick.Y);
	}
}

void AKiteRiderPawn::UpdateMouseBar()
{
	// The mouse is the bar while the right button is held: sideways steers, towards you sheets
	// in. Letting go centres the bar.
	const APlayerController* PC = Cast<APlayerController>(GetController());
	const bool bMouseBar = PC && PC->IsInputKeyDown(EKeys::RightMouseButton);
	if (bMouseBar)
	{
		float DeltaX = 0.0f;
		float DeltaY = 0.0f;
		PC->GetInputMouseDelta(DeltaX, DeltaY);
		MouseSteerInput = FMath::Clamp(MouseSteerInput + DeltaX * MouseSteerSensitivity, -1.0f, 1.0f);
		if (DeltaY != 0.0f)
		{
			// The mouse keeps its own mapping: the bar stays where it puts it, spring or not.
			ReleasePlayerSheetSpring();
			if (bMotionBarActive)
			{
				MotionBarMapping.SheetAtNeutral = FMath::Clamp(MotionBarMapping.SheetAtNeutral - DeltaY * MouseSheetSensitivity, 0.0f, 1.0f);
				ApplySheet(GetMotionSheet());
			}
			else
			{
				ApplySheet(CurrentSheetInput - DeltaY * MouseSheetSensitivity);
			}
		}
		SteerKite(KeySteerInput + MouseSteerInput);
	}
	else if (MouseSteerInput != 0.0f)
	{
		MouseSteerInput = 0.0f;
		SteerKite(KeySteerInput);
	}

	if (Kite)
	{
		Kite->SetLoopHeld(bScriptedRawSteer);
	}
}

void AKiteRiderPawn::OnSheetTriggered(const FInputActionValue& Value)
{
	// The player's keys, stick and triggers: spring-loaded about the middle when the setting is on
	// (UpdateBarSheet).
	SheetRateInput = FMath::Clamp(Value.Get<float>(), -1.0f, 1.0f);
	bPlayerSheetInput = true;
}

void AKiteRiderPawn::SetSheetRateInput(float Axis)
{
	// Scripted: the bar stays where it is put, the input moves it in or out (UpdateBarSheet).
	ReleasePlayerSheetSpring();
	SheetRateInput = FMath::Clamp(Axis, -1.0f, 1.0f);
}

void AKiteRiderPawn::OnEdgeTriggered(const FInputActionValue& Value)
{
	PlayerRiderStick.X = FMath::Clamp(Value.Get<float>(), -1.0f, 1.0f);
	bPlayerRiderInput = true;
	bScriptedGrabZoneStick = false;
	UpdateScreenBackSignLatch();
	RoutePlayerRiderInput();
}

void AKiteRiderPawn::OnJumpPressed(const FInputActionValue& Value)
{
	bPlayerRiderInput = true;
	// Pressed in the air: the tuck, held as long as the button is. The board's crouch for the landing
	// comes with it, as before.
	bPlayerTuckHeld = BoardMovement && BoardMovement->GetBoardState() == EBoardState::Airborne;
	SetLoadHeld(true);
	UpdateScreenBackSignLatch();
	RoutePlayerRiderInput();
}

void AKiteRiderPawn::OnJumpReleased(const FInputActionValue& Value)
{
	bPlayerRiderInput = true;
	bPlayerTuckHeld = false;
	ReleaseLoadAndPop();
	UpdateScreenBackSignLatch();
	RoutePlayerRiderInput();
}

void AKiteRiderPawn::OnRotatePressed(const FInputActionValue& Value)
{
	bPlayerRiderInput = true;
	bRotateHeld = true;
	RoutePlayerRiderInput();
}

void AKiteRiderPawn::OnRotateReleased(const FInputActionValue& Value)
{
	bPlayerRiderInput = true;
	bRotateHeld = false;
	RoutePlayerRiderInput();
}

void AKiteRiderPawn::SetLoadHeld(bool bHeld)
{
	if (BoardMovement)
	{
		BoardMovement->SetLoadHeld(bHeld);
	}
}

bool AKiteRiderPawn::ReleaseLoadAndPop()
{
	// The pop uses the load that has built up, so it comes before the crouch is let go.
	const bool bWasHeld = BoardMovement && BoardMovement->IsLoadHeld();
	const bool bAirborne = BoardMovement && BoardMovement->GetBoardState() == EBoardState::Airborne;
	bool bPopped = false;
	// If the kite has already pulled the rider off the water there is nothing to pop from, and no
	// message is needed for that.
	if (bWasHeld && !bAirborne)
	{
		bPopped = Jump();
	}
	SetLoadHeld(false);
	return bPopped;
}

bool AKiteRiderPawn::Jump()
{
	if (BoardMovement)
	{
		const EJumpRejectReason Reason = BoardMovement->Jump();
		if (Reason != EJumpRejectReason::None)
		{
			if (APlayerController* PC = Cast<APlayerController>(GetController()))
			{
				if (AKiteSurfHUD* HUD = Cast<AKiteSurfHUD>(PC->GetHUD()))
				{
					HUD->ShowJumpRejection(Reason);
				}
			}
			return false;
		}

		// Play pop bass thump
		PlayOneShot(PopSound, 0.8f);
		PlayHaptic(0.5f, 0.1f, false);
		return true;
	}
	return false;
}

void AKiteRiderPawn::EdgeBoard(float Axis)
{
	const float ClampedAxis = FMath::Clamp(Axis, -1.0f, 1.0f);
	if (BoardMovement)
	{
		BoardMovement->SetEdgeInput(ClampedAxis);
	}
}

void AKiteRiderPawn::SteerKite(float Axis)
{
	CurrentSteerInput = FMath::Clamp(Axis, -1.0f, 1.0f);
	if (Kite)
	{
		Kite->SteerKite(CurrentSteerInput);
	}
}

void AKiteRiderPawn::SheetKite(float Amount)
{
	// Scripted: the bar holds this position, so the player's spring lets go of it.
	ReleasePlayerSheetSpring();
	ApplySheet(Amount);
}

void AKiteRiderPawn::ApplySheet(float Amount)
{
	CurrentSheetInput = FMath::Clamp(Amount, 0.0f, 1.0f);
	// Unhooked, the chicken loop rides up to the stopper and the trim is fixed there: whatever moves
	// the bar (the stick, the triggers, the keys, the mouse, the motion bar, the spring) moves the
	// arms instead (docs/tricks/T3.md 1.2 and its addendum).
	ArmExtension = ArmExtensionForBar(CurrentSheetInput, BarNeutralSheet, UnhookedArmExtensionDefault);
	if (Kite)
	{
		Kite->SheetKite(Bar.bHooked ? CurrentSheetInput : UnhookedStopperSheet);
	}
}

float AKiteRiderPawn::ArmExtensionForBar(float BarPosition, float Neutral, float DefaultExtension)
{
	const float N = FMath::Clamp(Neutral, 0.01f, 0.99f);
	const float B = FMath::Clamp(BarPosition, 0.0f, 1.0f);
	const float D = FMath::Clamp(DefaultExtension, 0.0f, 1.0f);
	return B <= N ? FMath::Lerp(1.0f, D, B / N) : FMath::Lerp(D, 0.0f, (B - N) / (1.0f - N));
}

FVector AKiteRiderPawn::GetBoardVelocity() const
{
	if (BoardMovement)
	{
		return BoardMovement->Velocity;
	}
	return BoardVelocity;
}

float AKiteRiderPawn::GetKiteAzimuthDeg() const
{
	if (Kite)
	{
		return Kite->GetAzimuthDeg();
	}
	return KiteAzimuthDeg;
}

void AKiteRiderPawn::StepSimulation(float StepSeconds)
{
	if (RootComponent)
	{
		PrevSimLocation = RootComponent->GetComponentLocation();
		PrevSimRotation = RootComponent->GetComponentQuat();
	}
	SimTimeSeconds += StepSeconds;

	// UKiteComponent computes the aerodynamic line force (kg*cm/s^2) with the rider where they
	// are; UBoardMovementComponent owns all velocity integration and hydrodynamics.
	if (Kite)
	{
		// The kite's assist flies differently with the rider in the air (the board's state from its
		// last step).
		Kite->SetRiderAirborne(BoardMovement && BoardMovement->GetBoardState() == EBoardState::Airborne);
		Kite->StepKite(StepSeconds);
	}
	if (Kite && BoardMovement)
	{
		BoardMovement->AddExternalForce(Kite->GetLineForce());
	}
	// The flip's arms in and the tantrum's back hand (T3.3), which the bar reads.
	StepFreestyleHands();
	// The bar (T3.1): with the tension the kite has just made. Before the attitude, which pulls at
	// the hands it puts the bar in, and the board, which grades a lost bar if it lands.
	StepBar(StepSeconds);
	// The hands and the back foot (T2.1, T2.2): before the attitude, which takes the grab's tuck, and
	// the board, which grades the foot if it lands.
	StepGrabs(StepSeconds);
	// The rider's rotation, with this step's line force and the board as it starts the step; the board
	// then turns and lands with it (docs/tricks/T1.md T1.2.7).
	StepRiderAttitude(StepSeconds);
	if (BoardMovement)
	{
		BoardMovement->StepBoard(StepSeconds);
		if (BoardMovement->GetBoardState() != EBoardState::Airborne)
		{
			// The load and the edge the rider takes off with: the pop zeroes the board's load.
			LastGroundLoad = BoardMovement->GetLoadAmount();
			LastGroundEdgeHold = FMath::Abs(BoardMovement->GetEdgeInput());
		}
	}
	// The riding stance (T3.5): after the board, which has just landed or not.
	StepStance(StepSeconds);
	if (TrickTracker)
	{
		TrickTracker->SetGrabSource(&GrabState);
		TrickTracker->SetBarSource(&Bar);
		TrickTracker->SetRaleyArms(HasRaleyArms());
		TrickTracker->StepTracker(StepSeconds);
	}

	if (RootComponent)
	{
		SimLocation = RootComponent->GetComponentLocation();
		SimRotation = RootComponent->GetComponentQuat();
	}
	bHasSimState = true;

	if (GetPhysicsDebugLevel() >= 2)
	{
		LogPhysicsTelemetry();
	}
}

FVector AKiteRiderPawn::ComputeLevelBodyUp(const FVector& Facing, const FVector& TowardsKite, bool bHasKite, bool bAirborne, float Load, float HarnessLean) const
{
	// Lean away from the pull of the lines, whichever way the rider is facing. On the water that
	// is leaning out against the kite; in the air the rider hangs from the harness at their
	// waist, so the lower the kite the further back the shoulders go. Floating, they lie back
	// in the water with the board out in front.
	FVector BodyUp = FVector::UpVector;
	if (bHasKite && Kite)
	{
		const float FullLeanForce = KiteUnits::NToUnrealForce(600.0f);
		const float LineLoad = FMath::Clamp(Kite->GetLineForce().Size() / FullLeanForce, 0.0f, 1.0f);
		float PullLeanDeg = RiderMaxLeanDeg * FMath::Clamp(Kite->GetLineForce().Size2D() / FullLeanForce, 0.0f, 1.0f);
		if (bAirborne)
		{
			const float LineOffVerticalDeg = 90.0f - Kite->GetElevationDeg();
			PullLeanDeg = FMath::Min(RiderAirHangLeanDeg, 0.6f * LineOffVerticalDeg) * LineLoad;
		}
		BodyUp -= TowardsKite * FMath::Tan(FMath::DegreesToRadians(PullLeanDeg));
	}
	if (BoardMovement && BoardMovement->FloatSubmersionCm > 0.0f)
	{
		const float FloatLeanDeg = RiderFloatLeanDeg * FMath::Clamp(BoardMovement->GetFloatDepthCm() / BoardMovement->FloatSubmersionCm, 0.0f, 1.0f);
		BodyUp -= Facing * FMath::Tan(FMath::DegreesToRadians(FloatLeanDeg));
	}
	// Loading: the rider sits back away from the kite, weight low over the back of the board. Holding
	// the carve against the harness's limit (the board pointed as far from the pull as the body can
	// twist), they lean back against the hook the same way.
	if (Load > 0.0f || HarnessLean > 0.0f)
	{
		const FVector Away = bHasKite ? -TowardsKite : -Facing;
		BodyUp += Away * (FMath::Tan(FMath::DegreesToRadians(RiderLoadLeanDeg * Load)) + FMath::Tan(FMath::DegreesToRadians(RiderHarnessLeanDeg * HarnessLean)));
	}
	return BodyUp;
}

FQuat AKiteRiderPawn::ComputeSlavedBodyQuat() const
{
	// The riding pose from the simulation's own state (the root as stepped, the kite where it is),
	// so the take-off is the same at any frame rate. Up is the lean line exactly: the pelvis sits on
	// it in the drawn pose, so the attitude starts where the drawn rider stands.
	const float RootYawDeg = RootComponent ? static_cast<float>(RootComponent->GetComponentRotation().Yaw) : 0.0f;
	const FVector Facing = FRotator(0.0f, RootYawDeg + 90.0f * RiderStanceSide + RiderTurnOffsetDeg, 0.0f).Vector();
	const bool bHasKite = HasKitePosition();
	const FVector TowardsKite = bHasKite ? (Kite->GetKiteWorldPosition() - GetActorLocation()).GetSafeNormal2D() : FVector::ZeroVector;
	const FVector BodyUp = ComputeLevelBodyUp(Facing, TowardsKite, bHasKite, false, BoardMovement ? BoardMovement->GetLoadAmount() : 0.0f,
		BoardMovement ? BoardMovement->GetHarnessLeanAmount() : 0.0f).GetSafeNormal();
	return FRotationMatrix::MakeFromZX(BodyUp.IsNearlyZero() ? FVector::UpVector : BodyUp, Facing).ToQuat();
}

void AKiteRiderPawn::StepGrabs(float StepSeconds)
{
	FGrabStateInput In;
	In.bFront = bGrabFrontHeld;
	In.bBack = bGrabBackHeld;
	In.bOneFoot = bOneFootHeld;
	In.bAirborne = BoardMovement && BoardMovement->GetBoardState() == EBoardState::Airborne;
	In.ZoneStick = GrabZoneStick;
	GrabState.Step(In, StepSeconds);
	if (GrabState.DidHandArriveThisStep())
	{
		// The hand closes on the board.
		PlayHaptic(0.2f, 0.05f, false);
	}
	if (GrabState.GetBoardOff().DidCatchThisStep())
	{
		// The feet are back in the straps (T2.3).
		PlayHaptic(0.35f, 0.08f, false);
	}
	if (BoardMovement)
	{
		BoardMovement->SetRiderBackFoot(GrabState.GetFootAtTouchdown());
		BoardMovement->SetRiderBoardCatch(GrabState.GetBoardOff().GetCatchAtTouchdown());
	}
}

FQuat AKiteRiderPawn::GetBarBodyQuat() const
{
	if (RiderAttitude && bUseRiderAttitude && RiderAttitude->IsSimulating())
	{
		return RiderAttitude->GetBodyQuat();
	}
	// On the water: the riding pose, with the rail the rider faces picked here for the root as it is
	// now. The stance side is otherwise updated once a frame (UpdateRiderPose), so for the rest of a
	// frame in which the board swaps ends under the rider the slaved pose would face the other way
	// and the lines would seem to wrap half way round the body.
	const float RootYawDeg = RootComponent ? static_cast<float>(RootComponent->GetComponentRotation().Yaw) : 0.0f;
	const float Side = ChooseStanceSide(RootYawDeg, RiderFacingYawDeg);
	const FVector Facing = FRotator(0.0f, RootYawDeg + 90.0f * Side + RiderTurnOffsetDeg, 0.0f).Vector();
	return FRotationMatrix::MakeFromXZ(Facing, FVector::UpVector).ToQuat();
}

float AKiteRiderPawn::GetBarNoseSideSign() const
{
	if (RiderAttitude && bUseRiderAttitude && RiderAttitude->IsSimulating())
	{
		return RiderAttitude->GetStrapOffset().GetAxisX().Y >= 0.0 ? 1.0f : -1.0f;
	}
	if (RidingStance != ETrickStance::Heelside)
	{
		// A held toeside or blind stance (T3.5): the straps' side from the touchdown. The physics body did
		// not turn in the air, so its nose is on the other side of the body now, but the feet are where
		// they were, and a turn on towards the back foot is still backside.
		return StanceNoseSideSign;
	}
	// The board's nose against the body's right, both as GetBarBodyQuat has them.
	const FVector Nose = RootComponent ? RootComponent->GetForwardVector() : FVector::ForwardVector;
	return FVector::DotProduct(Nose, GetBarBodyQuat().GetAxisY()) >= 0.0 ? 1.0f : -1.0f;
}

void AKiteRiderPawn::ResetBar()
{
	const bool bWasUnhooked = !Bar.bHooked;
	Bar = FBarState();
	bHookPressPending = false;
	bPassPressPending = false;
	bFlipArmsIn = false;
	bFlipArmsOverridden = false;
	bTantrumHandOff = false;
	if (BoardMovement)
	{
		BoardMovement->SetRiderBarInHands(true);
	}
	if (bWasUnhooked)
	{
		// Off the leash and out of the low park; the bar's position goes back to the kite's sheet, and
		// the motion bar takes the pad as it is held now. A reset while hooked in touches none of it.
		if (Kite)
		{
			Kite->SetLeashed(false);
			Kite->SetLowParkAssist(false, LowParkElevationDeg);
		}
		ApplySheet(CurrentSheetInput);
		RecentreMotionBar();
		++HookRecentreCount;
	}
}

bool AKiteRiderPawn::HasRaleyArms() const
{
	return !Bar.bHooked && Bar.Place == EBarPlace::Front && Bar.Hands == EBarHands::Both && GetArmExtension() >= RaleyArmExtension;
}

bool AKiteRiderPawn::IsFlipPreWind(bool* OutBack) const
{
	if (OutBack)
	{
		*OutBack = false;
	}
	if (PreWindAmount <= 0.0f || PreWindDirection.IsNearlyZero() || !RiderAttitude)
	{
		return false;
	}
	// The attitude's own stick mapping, so the flip sector is the one the take-off will use.
	const RiderAxes::FRotationAxisChoice Choice = RiderAxes::ChooseAxisBody(PreWindDirection, 1.0f, RiderAttitude->DefaultRollAxisTiltDeg,
		RiderAttitude->RollAxisTiltRangeDeg, RiderAttitude->FlipSectorDeg, RiderAttitude->SpinAxisTiltMaxDeg, RiderAttitude->FlipSectorHysteresisDeg, false);
	if (Choice.Family != RiderAxes::ERotationFamily::Flip)
	{
		return false;
	}
	if (OutBack)
	{
		*OutBack = PreWindDirection.Y < 0.0f;
	}
	return true;
}

void AKiteRiderPawn::StepFreestyleHands()
{
	const bool bAirborne = BoardMovement && BoardMovement->GetBoardState() == EBoardState::Airborne;
	const bool bTakeoff = bAirborne && !bFreestyleHandsWasAirborne;
	bFreestyleHandsWasAirborne = bAirborne;
	const bool bUnhookedWithBar = !Bar.bHooked && Bar.Place != EBarPlace::Lost;
	bool bBackFlip = false;
	const bool bFlip = IsFlipPreWind(&bBackFlip);

	// The flip's arms in: wound up on the water with a flip pre-wind, kept to the touchdown. Moving the
	// bar from where it was takes the arms back for the rest of the jump.
	if (!bUnhookedWithBar || (!bAirborne && !(BoardMovement && BoardMovement->IsLoadHeld())))
	{
		bFlipArmsIn = false;
		bFlipArmsOverridden = false;
	}
	else if (!bAirborne && bFlip && !bFlipArmsIn && !bFlipArmsOverridden)
	{
		bFlipArmsIn = true;
		FlipArmsBarAtStart = CurrentSheetInput;
	}
	else if (!bAirborne && !bFlip && bFlipArmsIn)
	{
		// The stick left the flip sector during the wind-up.
		bFlipArmsIn = false;
	}
	if (bFlipArmsIn && FMath::Abs(CurrentSheetInput - FlipArmsBarAtStart) > 0.05f)
	{
		bFlipArmsIn = false;
		bFlipArmsOverridden = true;
	}

	// The tantrum: the back hand comes off as the rider leaves the water on a backflip pre-wind, and
	// goes back on the bar when the touchdown is near.
	if (!bAirborne || !bUnhookedWithBar)
	{
		bTantrumHandOff = false;
	}
	else if (bTakeoff && bTantrumBackHandOff && bFlip && bBackFlip)
	{
		bTantrumHandOff = true;
	}
	if (bTantrumHandOff && RiderAttitude && RiderAttitude->IsSimulating()
		&& RiderAttitude->GetLastStepDebug().TimeToContactSeconds < RegrabBeforeContactSeconds)
	{
		bTantrumHandOff = false;
	}
}

void AKiteRiderPawn::StepBar(float StepSeconds)
{
	if (!BoardMovement || !Kite || StepSeconds <= 0.0f)
	{
		return;
	}
	// A reset (the reset button, or the end of a crash) puts the rider back hooked in with the bar.
	// Polled, so a pawn whose delegates are not bound (tests) does the same.
	if (BoardMovement->GetResetCount() != SeenBarResetCount)
	{
		SeenBarResetCount = BoardMovement->GetResetCount();
		ResetBar();
	}

	const bool bAirborne = BoardMovement->GetBoardState() == EBoardState::Airborne;
	const bool bSlidingRound = !FMath::IsNearlyZero(RiderTurnOffsetDeg, 0.5f);
	if (!Bar.bHooked && !bAirborne && (BoardMovement->IsFloating() || bSlidingRound) && Bar.Place != EBarPlace::Passing && Bar.Place != EBarPlace::Lost)
	{
		// Floating in the water, or sliding the board round to face the kite after riding with the back
		// to it (UpdateRiderPose, StartStanceTurn), the rider turns to face the kite: the lines come back
		// in front rather than wrapping round them. This is how an unhooked rider who lands blind or
		// toeside gets back to heelside, at the end of the stance's hold or on X (T3.5).
		Bar.WrapDeg = 0.0f;
		Bar.bRouteBehind = false;
		Bar.bHasLineAngle = false;
		Bar.Place = EBarPlace::Front;
	}

	FBarInputs In;
	In.TensionN = Kite->GetLineTensionN();
	In.BodyWeightN = FMath::Max(BoardMovement->MassKg, 1.0f) * KiteUnits::GravityMS2;
	In.LineDirWorld = (Kite->GetKiteWorldPosition() - GetActorLocation()).GetSafeNormal();
	In.Body = GetBarBodyQuat();
	In.NoseSideSign = GetBarNoseSideSign();
	In.bAirborne = bAirborne;
	In.bOnWaterRideable = !bAirborne && !BoardMovement->IsCrashing();
	In.bHookPressed = bHookPressPending;
	In.bPassPressed = bPassPressPending;
	LastBarNoseSideSign = In.NoseSideSign;
	if (bPassPressPending && !bAirborne && !Bar.bHooked && Bar.Place != EBarPlace::Passing && Bar.Place != EBarPlace::Lost)
	{
		// X on the water by stance (T3.5). Toeside: the half turn back to heelside, no pass. Blind with
		// the lines passed round in the air: the half turn on, once the tension allows (StepStance). Blind
		// with no pass yet: the bar machine's surface pass, and the half turn when it is done (below).
		if (RidingStance == ETrickStance::Toeside)
		{
			In.bPassPressed = false;
			UE_LOG(LogKiteSurf, Log, TEXT("Stance: toeside to heelside on X (wrap %.0f deg)"), Bar.WrapDeg);
			StartUnwindingStanceTurn();
		}
		else if (RidingStance == ETrickStance::Blind && Bar.WrapDeg < 0.0f)
		{
			In.bPassPressed = false;
			StanceTurnBufferLeft = BarTunables.PassRequestBufferSeconds;
		}
	}
	bHookPressPending = false;
	bPassPressPending = false;
	const bool bWaterPassUnderWay = Bar.Place == EBarPlace::Passing && Bar.bPassFromWater;
	const bool bWaterPassJoinsJump = bWaterPassUnderWay && Bar.bPassJoinsJump;
	// One hand off the bar in the air (T3.3): a grab button takes that hand off first (a one-hand
	// grab, with the hand on its way back after the button is let go), and the tantrum's back hand.
	// Hooked in, the bar machine keeps both hands on it.
	if (bAirborne && !Bar.bHooked)
	{
		const bool bGrabHandOff = GrabState.IsHandOffBar();
		In.bReleaseFront = bGrabFrontHeld || (bGrabHandOff && GrabState.GetHand() == ETrickHand::Front);
		In.bReleaseBack = bGrabBackHeld || (bGrabHandOff && GrabState.GetHand() == ETrickHand::Back) || bTantrumHandOff;
	}

	const FBarEvents Events = BarStateMachine::Step(Bar, In, BarTunables, StepSeconds);

	if (Events.bUnhooked || Events.bHooked)
	{
		// Unhooked, the kite parks low and the sheet is held at the stopper; hooked again, the bar's
		// position is the kite's sheet once more. Nothing jumps: the motion bar takes the pad as it is
		// held now for the bar where it is.
		Kite->SetLowParkAssist(Events.bUnhooked, LowParkElevationDeg);
		ApplySheet(CurrentSheetInput);
		RecentreMotionBar();
		++HookRecentreCount;
		PlayHaptic(0.3f, 0.06f, false);
		UE_LOG(LogKiteSurf, Log, TEXT("Bar: %s (tension %.0f N, bar at %.2f, arms %.2f)"), Events.bUnhooked ? TEXT("unhooked") : TEXT("hooked in"),
			In.TensionN, CurrentSheetInput, GetArmExtension());
	}
	if (Events.bPassStarted)
	{
		if (bAirborne && bFlickAssist)
		{
			// The flick: the kite dips for a moment so the lines go slack for the bar to go round.
			Kite->RequestFlick(Kite->FlickSeconds);
		}
		UE_LOG(LogKiteSurf, Log, TEXT("Bar: pass started (%s, wrap %.0f deg, tension %.0f N)"), bAirborne ? TEXT("air") : TEXT("water"), Bar.WrapDeg, In.TensionN);
	}
	if (Events.bPassDone)
	{
		PlayHaptic(0.25f, 0.05f, false);
		UE_LOG(LogKiteSurf, Log, TEXT("Bar: pass done (%s), wrap now %.0f deg"), Events.PassKind == ETrickPassKind::Surface ? TEXT("surface") : TEXT("air"), Bar.WrapDeg);
		if (bWaterPassUnderWay && !bAirborne && RidingStance == ETrickStance::Blind)
		{
			// The surface pass from riding blind (T3.5): the bar is round, and the rider turns on backside
			// to heelside, the lines unwinding as they come round.
			UE_LOG(LogKiteSurf, Log, TEXT("Stance: blind to heelside after a surface pass (%s)"),
				bWaterPassJoinsJump ? TEXT("within the grace: it joins the jump") : TEXT("after the grace: not part of the jump"));
			StartUnwindingStanceTurn();
		}
	}
	if (Events.Lost != EBarLossCause::None)
	{
		// The bar is gone: the kite flags on its leash. On the water that is a crash now; in the air the
		// rider flies on and the landing crashes them (BarLost), as TriggerCrash would stop them dead.
		Kite->SetLowParkAssist(false, LowParkElevationDeg);
		Kite->SetLeashed(true);
		PlayHaptic(0.8f, 0.4f, true);
		UE_LOG(LogKiteSurf, Log, TEXT("Bar lost (%s): tension %.0f N (%.2f body weights), %s"), *UEnum::GetValueAsString(Events.Lost),
			In.TensionN, In.TensionN / In.BodyWeightN, bAirborne ? TEXT("in the air: crash at the touchdown") : TEXT("on the water: crash"));
	}
	else if (!Bar.bHooked && Bar.Place != EBarPlace::Lost && Bar.OverGripSeconds > 0.0f)
	{
		// Over the grip limit: a light buzz that rises as the bar slips.
		GripHapticCooldownSeconds -= StepSeconds;
		if (GripHapticCooldownSeconds <= 0.0f)
		{
			const float Slip = FMath::Clamp(Bar.OverGripSeconds / FMath::Max(BarTunables.GripLimitSeconds, 0.01f), 0.0f, 1.0f);
			PlayHaptic(0.2f + 0.5f * Slip, 0.05f, false);
			GripHapticCooldownSeconds = 0.05f;
		}
	}
	else
	{
		GripHapticCooldownSeconds = 0.0f;
	}
	if (Bar.Place == EBarPlace::Lost && !bAirborne && !BoardMovement->IsCrashing())
	{
		// Without the bar the rider cannot ride: a crash on the water at once, and after a bar lost in
		// the air on the touchdown that did not count as a landing (a skip off the surface); a counted
		// landing has already crashed through the landing evaluator (BarLost).
		BoardMovement->TriggerCrash();
	}
	BoardMovement->SetRiderBarInHands(Bar.Place != EBarPlace::Lost);
}

void AKiteRiderPawn::StepRiderAttitude(float StepSeconds)
{
	if (!BoardMovement)
	{
		return;
	}
	if (!RiderAttitude || !bUseRiderAttitude || StepSeconds <= 0.0f)
	{
		BoardMovement->SetAirAttitude(BoardMovement->GetBoardWorldQuat(), false);
		return;
	}

	const bool bAirborne = BoardMovement->GetBoardState() == EBoardState::Airborne;
	const bool bTakeoff = bAirborne && !RiderAttitude->IsSimulating();
	const FVector Velocity = BoardMovement->Velocity;
	const FVector Location = GetActorLocation();
	const FQuat RootQuat = RootComponent ? RootComponent->GetComponentQuat() : FQuat::Identity;

	// The vertical acceleration the time to contact needs, measured from the board's vertical speed
	// step to step and filtered: the kite makes the fall far from ballistic. The pop is an impulse,
	// so the take-off starts the filter from free fall rather than from the kick.
	if (bTakeoff || !bHasLastStepVerticalSpeed)
	{
		FilteredVerticalAccelCmS2 = -KiteUnits::GravityCmS2;
	}
	else
	{
		const float RawAccelCmS2 = (static_cast<float>(Velocity.Z) - LastStepVerticalSpeedCmS) / StepSeconds;
		FilteredVerticalAccelCmS2 += (RawAccelCmS2 - FilteredVerticalAccelCmS2) * (1.0f - FMath::Exp(-StepSeconds / FMath::Max(VerticalAccelFilterSeconds, 0.001f)));
	}
	LastStepVerticalSpeedCmS = static_cast<float>(Velocity.Z);
	bHasLastStepVerticalSpeed = true;

	// The pre-wind is wound up while the load is held on the water, in the last direction the stick
	// was held past the threshold, and kept until the take-off uses it (letting go of the stick as
	// the rider pops does not lose it); standing up again without leaving the water lets it go.
	if (!bAirborne)
	{
		if (BoardMovement->IsLoadHeld())
		{
			if (PreWindStick.Size() > PreWindStickThreshold)
			{
				PreWindAmount = FMath::Min(PreWindAmount + StepSeconds / FMath::Max(PreWindBuildSeconds, 0.01f), 1.0f);
				PreWindDirection = PreWindStick;
			}
			// Batch A: on the player path, letting IA_Rotate go while still loading does not lose the
			// pre-wind straight away (releasing the modifier and the jump button in the same frame
			// still gives the trick, since RoutePlayerRiderInput has already zeroed PreWindStick by
			// then and nothing here runs again before the take-off reads it). Past
			// RotateReleaseGraceSeconds it clears, so a modifier let go well before the pop leaves a
			// plain jump. The scripted setters (SetPreWind, kitesurf.PreWind) are not on the player
			// path and are never gated.
			if (bPlayerRiderInput && !bRotateHeld)
			{
				RotateReleaseElapsedSeconds += StepSeconds;
				if (RotateReleaseElapsedSeconds > RotateReleaseGraceSeconds)
				{
					PreWindAmount = 0.0f;
					PreWindDirection = FVector2D::ZeroVector;
				}
			}
			else
			{
				RotateReleaseElapsedSeconds = 0.0f;
			}
		}
		else
		{
			PreWindAmount = 0.0f;
			PreWindDirection = FVector2D::ZeroVector;
			RotateReleaseElapsedSeconds = 0.0f;
		}
	}

	FAttitudeInputs In;
	In.bAirborne = bAirborne;
	In.bStrapped = true;
	In.SlavedBodyQuat = ComputeSlavedBodyQuat();
	In.SlavedBoardQuat = RootQuat;
	In.VelocityCmS = Velocity;
	In.LineForceUU = Kite ? Kite->GetLineForce() : FVector::ZeroVector;
	In.bLinesTaut = Kite && Kite->AreLinesTaut();
	float WaterHeightCm = 0.0f;
	FVector WaterNormal = FVector::UpVector;
	BoardMovement->SampleWaterSurface(Location, WaterHeightCm, WaterNormal);
	In.HeightAboveWaterCm = static_cast<float>(Location.Z) - WaterHeightCm;
	In.VerticalAccelCmS2 = FilteredVerticalAccelCmS2;
	In.WaterNormal = WaterNormal;
	In.RotationStick = AirRotationStick;
	In.bRotationInput = AirRotationStick.Size() > AirRotationDeadzone;
	if (GrabState.IsZoneStickActive())
	{
		// The stick is picking a grab zone: no control torque, the rotation keeps its momentum.
		In.RotationStick = FVector2D::ZeroVector;
		In.bRotationInput = false;
	}
	// The grab tucks the body (its zone's tuck as the hand reaches), on top of the jump-held tuck.
	In.Tuck = FMath::Max(TuckInput, GrabState.GetTuckTarget());
	In.PreWindStick = PreWindDirection;
	In.PreWindAmount = PreWindAmount;
	In.TakeoffLoad = LastGroundLoad;
	In.TakeoffEdgeHold = LastGroundEdgeHold;
	if (bTakeoff)
	{
		TravelSideSigma = RiderAxes::TravelSide(In.SlavedBodyQuat, Velocity, RootQuat.GetAxisX());
	}
	In.TravelSideSigma = TravelSideSigma;
	// Unhooked, the lines pull at the hands (docs/tricks/T3.md 1.1): where the bar is for its state,
	// the arm extension and the line, in the body frame as it starts the step.
	In.bUseLineAttach = !Bar.bHooked && Bar.Place != EBarPlace::Lost;
	const FVector LineDirWorld = Kite ? (Kite->GetKiteWorldPosition() - Location).GetSafeNormal() : FVector::ZeroVector;
	if (In.bUseLineAttach && Kite)
	{
		const FQuat Body = RiderAttitude->GetBodyQuat();
		const FVector LineDirBody = Body.UnrotateVector(LineDirWorld);
		LineAttachBodyCm = LineAttach::AttachPointBody(Bar, LineDirBody, GetArmExtension(), GetBarNoseSideSign(), LineAttachTunables);
	}
	else
	{
		LineAttachBodyCm = RiderAttitude->HookOffsetFromComCm;
	}
	In.LineAttachBodyCm = LineAttachBodyCm;
	// Freestyle (T3.2, T3.3): hooked in a flip pre-wind is scaled down; unhooked with the arms out the
	// line swings the body out (the raley) and the roll input turns it about the lines (the S-bend).
	In.bHookedIn = Bar.bHooked;
	In.bRaleyArms = HasRaleyArms();
	In.LineDirWorld = LineDirWorld;
	In.ArmsOut = (In.bUseLineAttach && Bar.Place == EBarPlace::Front)
		? FMath::Clamp((GetArmExtension() - UnhookedArmExtensionDefault) / FMath::Max(1.0f - UnhookedArmExtensionDefault, 0.01f), 0.0f, 1.0f)
		: 0.0f;

	RiderAttitude->Step(StepSeconds, In);
	BoardMovement->SetAirAttitude(RiderAttitude->GetBoardQuat(), RiderAttitude->IsSimulating(), RiderAttitude->GetBodyQuat(), RiderAttitude->GetAngularVelocity());
}

void AKiteRiderPawn::ResetRiderAttitude()
{
	const FQuat RootQuat = RootComponent ? RootComponent->GetComponentQuat() : FQuat::Identity;
	if (RiderAttitude)
	{
		RiderAttitude->Reset(ComputeSlavedBodyQuat(), RootQuat);
	}
	if (BoardMovement)
	{
		BoardMovement->SetAirAttitude(RootQuat, false);
	}
	PreWindAmount = 0.0f;
	PreWindDirection = FVector2D::ZeroVector;
	bHasLastStepVerticalSpeed = false;
	RiderAirBlend = 0.0f;
	// The grab state is not reset here: a crash landing calls this from the board's crash event, before
	// the tracker reads this flight's grabs. Off the water's air state the hands and the foot go back
	// on their own (FGrabState).
	if (bAttitudeOwnsBoardVisual)
	{
		ClearBoardVisualOverride();
		if (BoardVisual)
		{
			BoardVisual->SetRelativeLocation(FVector::ZeroVector);
		}
		bAttitudeOwnsBoardVisual = false;
	}
}

void AKiteRiderPawn::UpdateBoardVisualFromAttitude(float DeltaTime)
{
	// A crash or a reset ends the rotation at once: no hand-over. Polled, as well as the board's
	// events, so a pawn whose delegates are not bound (tests) does the same.
	if (BoardMovement && (BoardMovement->IsCrashing() || BoardMovement->GetResetCount() != SeenAttitudeResetCount))
	{
		SeenAttitudeResetCount = BoardMovement->GetResetCount();
		RiderAirBlend = 0.0f;
		if (bAttitudeOwnsBoardVisual)
		{
			ClearBoardVisualOverride();
			if (BoardVisual)
			{
				BoardVisual->SetRelativeLocation(FVector::ZeroVector);
			}
			bAttitudeOwnsBoardVisual = false;
		}
	}
	const bool bLive = RiderAttitude && RiderAttitude->IsSimulating();
	const float BlendStep = RiderHandoverSeconds > 0.0f ? DeltaTime / RiderHandoverSeconds : 1.0f;
	RiderAirBlend = bLive ? FMath::Min(RiderAirBlend + BlendStep, 1.0f) : FMath::Max(RiderAirBlend - BlendStep, 0.0f);
	if (!BoardVisual)
	{
		return;
	}
	if (bLive)
	{
		// The strapped board, drawn between the last two steps. The rider turns about their centre
		// of mass, not about their feet: the board sits ComAboveBoardCm below it along the body
		// (T1.2.6), which is the root itself at the take-off and again when upright for the landing.
		const FQuat Body = RiderAttitude->GetRenderBodyQuat(LastRenderAlpha);
		const FQuat Board = RiderAttitude->GetRenderBoardQuat(LastRenderAlpha);
		const FVector Offset = RiderAttitude->GetVisualComOffsetCm(LastRenderAlpha) - Body.RotateVector(FVector(0.0f, 0.0f, RiderAttitude->ComAboveBoardCm));
		LastAirBodyQuat = Body;
		LastAirBoardQuat = Board;
		LastAirBoardOffsetCm = Offset;
		SetBoardVisualWorldRotation(Board);
		BoardVisual->SetWorldLocation(GetActorLocation() + Offset);
		bAttitudeOwnsBoardVisual = true;
	}
	else if (bAttitudeOwnsBoardVisual)
	{
		if (RiderAirBlend > 0.0f)
		{
			// Landed: the drawn board eases back onto the root as the rider hands back to the riding pose.
			const float W = FMath::SmoothStep(0.0f, 1.0f, RiderAirBlend);
			SetBoardVisualWorldRotation(FQuat::Slerp(GetActorQuat(), LastAirBoardQuat, W));
			BoardVisual->SetWorldLocation(GetActorLocation() + LastAirBoardOffsetCm * W);
		}
		else
		{
			ClearBoardVisualOverride();
			BoardVisual->SetRelativeLocation(FVector::ZeroVector);
			bAttitudeOwnsBoardVisual = false;
		}
	}
}

int32 AKiteRiderPawn::GetPhysicsDebugLevel() const
{
#if UE_BUILD_SHIPPING
	return 0;
#else
	const int32 Level = CVarKitePhysicsDebug.GetValueOnGameThread();
	return (Kite && Kite->bDrawDebug) ? FMath::Max(Level, 1) : Level;
#endif
}

void AKiteRiderPawn::LogPhysicsTelemetry()
{
#if !UE_BUILD_SHIPPING
	if (!Kite || !BoardMovement)
	{
		return;
	}
	if (!bLoggedTelemetryHeader)
	{
		UE_LOG(LogKiteSurf, Log, TEXT("kitecsv,t_s,rider_x,rider_y,rider_z,rider_vx,rider_vy,rider_vz,kite_x,kite_y,kite_z,kite_vx,kite_vy,kite_vz,tension_n,alpha_deg,cl,board_state,gust,heel_deg,leeway_deg,side_n,normal_side_n,board_drag_n,water_z,surface_vz,water_up_n,absorbing,yaw_deg,upwind_of_beam_deg,harness,harness_lean,stance"));
		bLoggedTelemetryHeader = true;
	}
	// Positions in cm and velocities in cm/s, as the engine has them.
	const FVector RiderPos = SimLocation;
	const FVector RiderVel = BoardMovement->Velocity;
	const FVector KitePos = Kite->GetKiteWorldPosition();
	const FVector KiteVel = Kite->GetKiteVelocity();
	const FKiteStepDebug& KiteStep = Kite->GetLastStepDebug();
	const FBoardStepDebug& BoardStep = BoardMovement->GetLastStepDebug();
	const float Gust = Wind ? Wind->GetGustFactorAtTime(RiderPos, Kite->GetSimTimeSeconds()) : 1.0f;
	// The board's heel and leeway (deg), and the water's side force from leeway, the sideways part of
	// its normal force and its drag (N), signed across the board (positive to its right) or along it.
	const FVector BoardRight = FVector::CrossProduct(FVector::UpVector, FRotator(0.0f, SimRotation.Rotator().Yaw, 0.0f).Vector());
	// Then the water under the board (cm), how fast it rises there (cm/s), the water's vertical force
	// on the board (N) and whether the touchdown absorber is on. Then the board's heading (deg), its
	// angle upwind of the beam reach of the pull, whether the harness limited it (1, or 2 with the carve
	// held against the limit), the rider's lean back against the harness (0..1) and the rail they face
	// (+1 the board's right).
	const int32 HarnessState = BoardStep.bHarnessActive ? (BoardStep.bAgainstHarness ? 2 : 1) : 0;
	UE_LOG(LogKiteSurf, Log, TEXT("kitecsv,%.4f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.2f,%.3f,%d,%.3f,%.2f,%.2f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%d,%.2f,%.2f,%d,%.3f,%.0f"),
		SimTimeSeconds, RiderPos.X, RiderPos.Y, RiderPos.Z, RiderVel.X, RiderVel.Y, RiderVel.Z,
		KitePos.X, KitePos.Y, KitePos.Z, KiteVel.X, KiteVel.Y, KiteVel.Z,
		Kite->GetLineTensionN(), KiteStep.AlphaDeg, KiteStep.LiftCoefficient, static_cast<int32>(BoardMovement->GetBoardState()), Gust,
		BoardStep.HeelDeg, BoardStep.LeewayDeg, FVector::DotProduct(BoardStep.SideForceN, BoardRight), FVector::DotProduct(BoardStep.NormalSideForceN, BoardRight), BoardStep.DragForceN.Size(),
		BoardStep.WaterHeightCm, BoardStep.SurfaceVerticalSpeedCmS, BoardStep.WaterVerticalForceN, BoardStep.bAbsorbing ? 1 : 0,
		FRotator::NormalizeAxis(SimRotation.Rotator().Yaw), BoardStep.UpwindOfBeamDeg, HarnessState, BoardMovement->GetHarnessLeanAmount(), RiderStanceSide);
#endif
}

void AKiteRiderPawn::DrawPhysicsDebug() const
{
#if !UE_BUILD_SHIPPING
	const UWorld* World = GetWorld();
	if (!World || !Kite || !BoardMovement)
	{
		return;
	}
	const float SimTime = Kite->GetSimTimeSeconds();

	// At the kite: the air it flies in and what that air does to it, drawn where the canopy is drawn.
	const FKiteStepDebug& KiteStep = Kite->GetLastStepDebug();
	const FVector KiteAt = Kite->GetKiteMesh() ? Kite->GetKiteMesh()->GetComponentLocation() : Kite->GetKiteWorldPosition();
	const FVector LineStart = HarnessHookPosition.IsZero() ? GetActorLocation() : HarnessHookPosition;
	const bool bTaut = Kite->AreLinesTaut();
	DrawDebugLine(World, LineStart, KiteAt, bTaut ? FColor::Yellow : FColor::Red, false, -1.0f, 0, 1.0f);
	DrawDebugVector(World, KiteAt, KiteStep.TrueWindCmS / KiteUnits::CmPerM, DebugCmPerMS, FColor::Blue);
	DrawDebugVector(World, KiteAt, KiteStep.ApparentWindCmS / KiteUnits::CmPerM, DebugCmPerMS, FColor::Cyan);
	DrawDebugVector(World, KiteAt, KiteStep.LiftN, DebugCmPerN, FColor::Green);
	DrawDebugVector(World, KiteAt, KiteStep.DragN, DebugCmPerN, FColor::Red);
	DrawDebugVector(World, KiteAt, KiteStep.SideN, DebugCmPerN, FColor::Magenta);
	DrawDebugVector(World, KiteAt, (LineStart - KiteAt).GetSafeNormal() * Kite->GetLineTensionN(), DebugCmPerN, bTaut ? FColor::Yellow : FColor::Red);
	DrawDebugVector(World, KiteAt, Kite->GetKiteHeading(), DebugHeadingLengthCm, FColor::White);
	const FString KiteText = FString::Printf(TEXT("%.0f m^2 (%.1f projected)\nairspeed %.1f m/s  alpha %.1f deg\nCl %.2f  Cd %.2f\ntension %.0f N %s\nclock %.0f  depth %.0f deg\nsteps last frame %d"),
		Kite->AreaM2, Kite->GetProjectedAreaM2(), Kite->GetAirspeedCmS() / KiteUnits::CmPerM, KiteStep.AlphaDeg, KiteStep.LiftCoefficient, KiteStep.DragCoefficient,
		Kite->GetLineTensionN(), bTaut ? TEXT("taut") : TEXT("SLACK"), Kite->GetClockDeg(), Kite->GetWindowDepthDeg(), LastFrameSimSteps);
	DrawDebugString(World, KiteAt + FVector(0.0f, 0.0f, DebugKiteTextHeightCm), KiteText, nullptr, FColor::White, 0.0f, true);

	// At the rider: the wind they feel at chest height, the pull of the lines on the harness, what
	// the water did to the board in the last step, and the air's drag on them in the air.
	const FVector RiderAt = GetActorLocation();
	const FVector Chest = RiderAt + FVector(0.0f, 0.0f, Kite->RiderWindHeightCm);
	const FVector BoardVelocityNow = BoardMovement->Velocity;
	const FVector RiderWind = Wind ? Wind->GetWindAtTime(Chest, SimTime) : FVector::ZeroVector;
	DrawDebugVector(World, Chest, RiderWind / KiteUnits::CmPerM, DebugCmPerMS, FColor::Blue);
	DrawDebugVector(World, Chest, (RiderWind - BoardVelocityNow) / KiteUnits::CmPerM, DebugCmPerMS, FColor::Cyan);
	DrawDebugVector(World, LineStart, Kite->GetLineForce() / KiteUnits::UnrealForcePerN, DebugCmPerN, FColor::Yellow);
	// The board: the side force the fins and rail make from leeway (orange), the sideways part of the
	// water's normal force on the heeled board (green), the hull's drag (red), and the board's normal
	// tilted by its heel (purple).
	const FBoardStepDebug& BoardStep = BoardMovement->GetLastStepDebug();
	const FVector BoardAt = RiderAt + FVector(0.0f, 0.0f, DebugBoardForceHeightCm);
	DrawDebugVector(World, BoardAt, BoardStep.SideForceN, DebugCmPerN, FColor::Orange);
	DrawDebugVector(World, BoardAt, BoardStep.NormalSideForceN, DebugCmPerN, FColor::Green);
	DrawDebugVector(World, BoardAt, BoardStep.DragForceN, DebugCmPerN, FColor::Red);
	const FVector BoardRight = FVector::CrossProduct(FVector::UpVector, FRotator(0.0f, GetActorRotation().Yaw, 0.0f).Vector());
	const float HeelRad = FMath::DegreesToRadians(BoardStep.HeelDeg);
	const FVector BoardNormal = FVector::UpVector * FMath::Cos(HeelRad) - BoardRight * FMath::Sin(HeelRad);
	DrawDebugVector(World, BoardAt, BoardNormal, DebugHeelNormalLengthCm, FColor::Purple);
	DrawDebugVector(World, Chest, BoardStep.AirDragN, DebugCmPerN, FColor::Silver); // in the air only
	// The water the board read: the five samples (centre, nose, tail, right and left rail) on the
	// surface, white, and the normal of the plane fitted to them from the centre one, cyan; while the
	// touchdown absorber is working, the samples are red.
	const FColor SampleColor = BoardStep.bAbsorbing ? FColor::Red : FColor::White;
	for (const FVector& Sample : BoardStep.WaterSamplesCm)
	{
		DrawDebugPoint(World, Sample, DebugWaterSamplePointSize, SampleColor, false, -1.0f, 0);
	}
	DrawDebugVector(World, BoardStep.WaterSamplesCm[0], BoardStep.WaterNormal, DebugWaterNormalLengthCm, FColor::Cyan);

	float HeadingDeg = FRotator::NormalizeAxis(GetActorRotation().Yaw);
	HeadingDeg = HeadingDeg < 0.0f ? HeadingDeg + 360.0f : HeadingDeg;
	const float Gust = Wind ? Wind->GetGustFactorAtTime(RiderAt, SimTime) : 1.0f;
	// The harness: the heading against the beam reach of the pull and its limit, and the lean back.
	const FString HarnessText = BoardStep.bHarnessActive
		? FString::Printf(TEXT("%+.0f deg off the pull's beam (limit %.0f)%s  lean %.2f"), BoardStep.UpwindOfBeamDeg, BoardMovement->MaxUpwindHeadingDeg, BoardStep.bAgainstHarness ? TEXT(", AGAINST") : TEXT(""), BoardMovement->GetHarnessLeanAmount())
		: FString::Printf(TEXT("harness free  lean %.2f"), BoardMovement->GetHarnessLeanAmount());
	const FString RiderText = FString::Printf(TEXT("%.1f kn  heading %.0f\n%s\nleeway %.1f deg  heel %.1f deg\nside %.0f N  normal %.0f N\n%s%s\nwater %.0f cm rising %.1f m/s, holds %.0f N%s\nlast landing %.1f g%s\ngust %.2f"),
		KiteUnits::CmSToKnots(BoardVelocityNow.Size2D()), HeadingDeg, *HarnessText, BoardStep.LeewayDeg, BoardStep.HeelDeg, BoardStep.SideForceN.Size(), BoardStep.NormalSideForceN.Size(),
		BoardStateName(BoardMovement->GetBoardState()), BoardMovement->IsFloating() ? TEXT(", floating") : TEXT(""),
		RiderAt.Z - BoardStep.WaterHeightCm, BoardStep.SurfaceVerticalSpeedCmS / KiteUnits::CmPerM, BoardStep.WaterVerticalForceN, BoardStep.bAbsorbing ? TEXT(", absorbing") : TEXT(""),
		BoardMovement->GetLastLandingG(), BoardMovement->WasLastLandingHot() ? TEXT(" HOT") : TEXT(""), Gust);
	DrawDebugString(World, RiderAt + FVector(0.0f, 0.0f, DebugRiderTextHeightCm), RiderText, nullptr, FColor::White, 0.0f, true);

	// The rider's rotation in the air: the body's axes at the centre of mass (front red, right green,
	// up blue), the committed axis (white), the line and assist torques (yellow, cyan, 1 cm per N*m)
	// and the time to contact.
	if (RiderAttitude && RiderAttitude->IsSimulating())
	{
		const FAttitudeDebug& Attitude = RiderAttitude->GetLastStepDebug();
		const FQuat Body = RiderAttitude->GetBodyQuat();
		const FVector Com = RiderAt + RiderAttitude->GetVisualComOffsetCm(1.0f);
		DrawDebugVector(World, Com, Body.GetAxisX(), 60.0f, FColor::Red);
		DrawDebugVector(World, Com, Body.GetAxisY(), 60.0f, FColor::Green);
		DrawDebugVector(World, Com, Body.GetAxisZ(), 60.0f, FColor::Blue);
		DrawDebugVector(World, Com, Attitude.CommittedAxisWorld, 100.0f, FColor::White);
		DrawDebugVector(World, Com, Attitude.LineTorqueNm, 1.0f, FColor::Yellow);
		DrawDebugVector(World, Com, Attitude.AssistTorqueNm, 1.0f, FColor::Cyan);
		const FString AttitudeText = FString::Printf(TEXT("spin %.0f deg/s  up %.2f\ncontact in %.2f s  error %.0f deg%s"),
			FMath::RadiansToDegrees(RiderAttitude->GetAngularVelocity().Size()), Body.GetAxisZ().Z,
			FMath::Min(Attitude.TimeToContactSeconds, 99.0f), Attitude.LandingErrorDeg, Attitude.bAssistActive ? TEXT("  ASSIST") : TEXT(""));
		DrawDebugString(World, Com + FVector(0.0f, 0.0f, 60.0f), AttitudeText, nullptr, FColor::White, 0.0f, true);
	}

	// The gust bar: grey from 0.5 to 1.5, a white tick at 1, filled to the gust factor here now.
	const FVector Side = FVector::CrossProduct(FVector::UpVector, GetActorForwardVector()).GetSafeNormal();
	const FVector BarFoot = RiderAt + FVector(0.0f, 0.0f, DebugGustBarBaseHeightCm) + Side * DebugGustBarSideOffsetCm;
	const float CmPerGust = DebugGustBarLengthCm / (DebugGustBarMax - DebugGustBarMin);
	auto BarPoint = [&](float Factor) { return BarFoot + FVector(0.0f, 0.0f, (FMath::Clamp(Factor, DebugGustBarMin, DebugGustBarMax) - DebugGustBarMin) * CmPerGust); };
	DrawDebugLine(World, BarPoint(DebugGustBarMin), BarPoint(DebugGustBarMax), FColor(128, 128, 128), false, -1.0f, 0, 2.0f);
	DrawDebugLine(World, BarPoint(1.0f) - Side * 20.0f, BarPoint(1.0f) + Side * 20.0f, FColor::White, false, -1.0f, 0, 2.0f);
	DrawDebugLine(World, BarPoint(DebugGustBarMin), BarPoint(Gust), Gust >= 1.0f ? FColor::Orange : FColor(120, 160, 255), false, -1.0f, 0, 3.0f * DebugLineThickness);
#endif
}

void AKiteRiderPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UpdateMotionBar(DeltaTime);

	// The keys, stick and triggers move the bar, springing back to the middle when the player lets go
	// (bBarReturnsToMiddle); they still adjust it while the motion controller has the bar.
	UpdateBarSheet(DeltaTime);

	if (GetController())
	{
		UpdateMouseBar();
	}

	// The left stick and the jump button go where the rider's state now says: a held stick moves
	// from the board to the pre-wind as the load starts, to the air stick at the take-off and back to
	// the board on landing, with no new input event. IA_Rotate (batch A) gates whether the stick
	// reaches the pre-wind or the air stick at all.
	UpdateScreenBackSignLatch();
	RoutePlayerRiderInput();

	// The rig is stepped at a fixed rate however long the frame was, so the ride is the same at
	// any frame rate, and a hitch slows it rather than breaking it. The root is drawn between the
	// last two steps and put back on the simulation's own transform before stepping again.
	const float Step = FMath::Max(SimStepSeconds, KINDA_SMALL_NUMBER);
	if (!bStepSimulation)
	{
		// Placed by hand: whatever is there now is the truth, and nothing is drawn between steps.
		SimAccumulatorSeconds = 0.0f;
		bHasSimState = false;
	}
	if (bHasSimState && bInterpolateRendering && RootComponent)
	{
		const bool bStillWhereWeDrewIt = RootComponent->GetComponentLocation().Equals(LastRenderLocation, 0.01f)
			&& RootComponent->GetComponentQuat().Equals(LastRenderRotation, 1e-4f);
		if (bStillWhereWeDrewIt)
		{
			RootComponent->SetWorldLocationAndRotation(SimLocation, SimRotation, false, nullptr, ETeleportType::TeleportPhysics);
		}
		else
		{
			// Something outside the step loop moved the rider (a reset, a test): that is the new truth.
			PrevSimLocation = SimLocation = RootComponent->GetComponentLocation();
			PrevSimRotation = SimRotation = RootComponent->GetComponentQuat();
		}
	}
	SimAccumulatorSeconds += FMath::Clamp(DeltaTime, 0.0f, MaxFrameSeconds);
	int32 Steps = 0;
	while (bStepSimulation && SimAccumulatorSeconds >= Step && Steps < MaxSimStepsPerFrame)
	{
		StepSimulation(Step);
		SimAccumulatorSeconds -= Step;
		++Steps;
	}
	if (Steps >= MaxSimStepsPerFrame)
	{
		SimAccumulatorSeconds = 0.0f; // a hitch: drop the rest of it rather than try to catch up
	}
	LastFrameSimSteps = Steps;
	const float RenderAlpha = (bHasSimState && bInterpolateRendering) ? FMath::Clamp(SimAccumulatorSeconds / Step, 0.0f, 1.0f) : 1.0f;
	LastRenderAlpha = RenderAlpha;
	if (bHasSimState && bInterpolateRendering && RootComponent)
	{
		LastRenderLocation = FMath::Lerp(PrevSimLocation, SimLocation, RenderAlpha);
		LastRenderRotation = FQuat::Slerp(PrevSimRotation, SimRotation, RenderAlpha);
		RootComponent->SetWorldLocationAndRotation(LastRenderLocation, LastRenderRotation, false, nullptr, ETeleportType::TeleportPhysics);
	}

	// The rider and the camera follow where the kite generally is, not every swing of a loop.
	if (HasKitePosition())
	{
		const FVector KiteOffset = Kite->GetKiteWorldPosition() - GetActorLocation();
		SmoothedKiteOffset = bViewInitialized && !SmoothedKiteOffset.IsNearlyZero()
			? FMath::VInterpTo(SmoothedKiteOffset, KiteOffset, DeltaTime, 2.0f)
			: KiteOffset;
	}

	// The drawn board first: the rider's feet go in its straps.
	UpdateBoardVisualFromAttitude(DeltaTime);
	UpdateRiderPose(DeltaTime);
	if (Kite)
	{
		// After the pose, which tells the kite where the bar is.
		Kite->UpdateVisuals(RenderAlpha);
	}
	UpdateCamera(DeltaTime);
	bViewInitialized = true;

	const FVector Vel = GetBoardVelocity();
	ensureAlwaysMsgf(!Vel.ContainsNaN(), TEXT("AKiteRiderPawn::Tick: BoardVelocity contains NaN or Inf: %s"), *Vel.ToString());

	UpdateAudioModulation(DeltaTime);
	UpdateTensionHaptic(Kite ? Kite->GetLineTensionN() : 0.0f, DeltaTime);

	const int32 DebugLevel = GetPhysicsDebugLevel();
	if (DebugLevel >= 1)
	{
		DrawPhysicsDebug();
	}
	if (DebugLevel < 2)
	{
		bLoggedTelemetryHeader = false; // the CSV header comes again when logging is next switched on
	}
}

void AKiteRiderPawn::SetRiderCharacter(ERiderCharacter InCharacter)
{
	RiderCharacter = RiderCharacter::FromIndex(static_cast<int32>(InCharacter));

	// The jointed riders share a rig; each has its own torso and limb parts.
	const TCHAR* RiderName = RiderCharacter == ERiderCharacter::Wetsuit ? TEXT("Wetsuit") : (RiderCharacter == ERiderCharacter::Robot ? TEXT("Robot") : TEXT("Santa"));
	auto SetPart = [RiderName](UStaticMeshComponent* Component, const TCHAR* PartName)
	{
		if (!Component)
		{
			return;
		}
		const FString MeshPath = FString::Printf(TEXT("/Game/Meshes/SM_Rider%s_%s"), RiderName, PartName);
		if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath))
		{
			Component->SetStaticMesh(Mesh);
		}
		Component->SetVisibility(true);
	};
	SetPart(RiderTorso, TEXT("Torso"));
	static const TCHAR* LimbPartNames[] = { TEXT("Thigh"), TEXT("Shin"), TEXT("UpperArm"), TEXT("Forearm") };
	for (int32 Index = 0; Index < RiderLimbs.Num(); ++Index)
	{
		SetPart(RiderLimbs[Index], LimbPartNames[Index % 4]);
	}
}

bool AKiteRiderPawn::HasKitePosition() const
{
	if (!Kite)
	{
		return false;
	}
	const float KiteDistance = FVector::Dist(Kite->GetKiteWorldPosition(), GetActorLocation());
	return FMath::IsNearlyEqual(KiteDistance, Kite->LineLengthCm, Kite->LineLengthCm * 0.5f);
}

void AKiteRiderPawn::SetRidingStance(ETrickStance Stance)
{
	if (Stance != RidingStance)
	{
		UE_LOG(LogKiteSurf, Log, TEXT("Stance: %s -> %s after %.2f s (wrap %.0f deg)"), *UEnum::GetDisplayValueAsText(RidingStance).ToString(),
			*UEnum::GetDisplayValueAsText(Stance).ToString(), StanceSeconds, Bar.WrapDeg);
		RidingStance = Stance;
		StanceSeconds = 0.0f;
	}
	if (Stance == ETrickStance::Heelside)
	{
		StanceTurnBufferLeft = 0.0f;
	}
}

void AKiteRiderPawn::StepStance(float StepSeconds)
{
	if (!BoardMovement)
	{
		return;
	}
	const bool bAirborne = BoardMovement->GetBoardState() == EBoardState::Airborne;
	const bool bTouchdown = !bAirborne && bStanceWasAirborne;
	bStanceWasAirborne = bAirborne;
	if (Bar.bHooked || Bar.Place == EBarPlace::Lost || BoardMovement->IsCrashing() || BoardMovement->IsFloating())
	{
		// Hooked in the harness hook is on the front and the rider slides round as always; without the
		// bar, crashing or floating there is no stance to hold.
		SetRidingStance(ETrickStance::Heelside);
		return;
	}
	if (bAirborne)
	{
		// The stance in the air is the attitude's; the touchdown sets it again.
		return;
	}
	// The stance the lines give: the body's heading against the kite (the wrap) and their route. A pass
	// still under way counts as done (the bar is behind the back either way).
	const bool bPassing = Bar.Place == EBarPlace::Passing;
	const ETrickStance FromWrap = BarStateMachine::StanceForWrap(BarStateMachine::EffectiveWrapDeg(Bar), Bar.bRouteBehind || bPassing);
	if (bTouchdown)
	{
		StanceSettleSecondsLeft = StanceSettleSeconds;
		// The physics body did not turn in the air: keep the straps' side for the bar while it is held.
		StanceNoseSideSign = LastBarNoseSideSign;
	}
	if (StanceSettleSecondsLeft > 0.0f)
	{
		// Just landed: the body comes off the attitude onto the rail it faces over the first steps, and the
		// stance is the one the lines give once it has (the touchdown step's body can still be a little
		// short of the rail).
		StanceSettleSecondsLeft = FMath::Max(StanceSettleSecondsLeft - StepSeconds, 0.0f);
		if (FromWrap != RidingStance)
		{
			SetRidingStance(FromWrap);
		}
		else if (!bTouchdown && RidingStance != ETrickStance::Heelside)
		{
			StanceSeconds += StepSeconds;
		}
		return;
	}
	if (RidingStance == ETrickStance::Heelside)
	{
		return;
	}
	if (FromWrap == ETrickStance::Heelside)
	{
		// Come round to face the kite some other way (a carve): heelside, nothing to slide.
		SetRidingStance(ETrickStance::Heelside);
		return;
	}
	StanceSeconds += StepSeconds;
	if (StanceTurnBufferLeft > 0.0f)
	{
		// Blind with the lines passed round in the air: the half turn on to heelside on X, once the pull
		// is low enough for the bar to come round (the surface pass's limit).
		const float TensionBW = Kite ? Kite->GetLineTensionN() / FMath::Max(BoardMovement->MassKg * KiteUnits::GravityMS2, 1.0f) : 0.0f;
		if (TensionBW < BarTunables.SurfacePassMaxTensionBW)
		{
			UE_LOG(LogKiteSurf, Log, TEXT("Stance: blind to heelside on X, the lines already passed (tension %.2f body weights)"), TensionBW);
			StartUnwindingStanceTurn();
			return;
		}
		StanceTurnBufferLeft = FMath::Max(StanceTurnBufferLeft - StepSeconds, 0.0f);
	}
	const float HoldSeconds = RidingStance == ETrickStance::Toeside ? ToesideHoldSeconds : BlindHoldSeconds;
	if (StanceSeconds >= HoldSeconds - 0.5f * StepSeconds && !bPassing)
	{
		// The hold is over: the slide round back to heelside, as for any rider with the back to the kite.
		UE_LOG(LogKiteSurf, Log, TEXT("Stance: %s held %.2f s, sliding round to heelside"), *UEnum::GetDisplayValueAsText(RidingStance).ToString(), StanceSeconds);
		StartUnwindingStanceTurn();
	}
}

void AKiteRiderPawn::StartUnwindingStanceTurn()
{
	// Turning so the wrap goes back towards 0: a wrap under 0 (toeside, or blind with the lines passed)
	// rises, which is the chest going towards the back foot, now at the board's leading end; a wrap over
	// 0 (blind, not passed) falls, back the way the backside 180 came.
	StartStanceTurn(BarStateMachine::EffectiveWrapDeg(Bar) < 0.0f);
}

void AKiteRiderPawn::StartStanceTurn(bool bViaTravelNose)
{
	const float BoardYawDeg = GetActorRotation().Yaw;
	const float OldSide = ChooseStanceSide(BoardYawDeg, RiderFacingYawDeg);
	// The board's leading end: its nose, unless it is moving tail first.
	const FVector Velocity2D = BoardMovement ? FVector(BoardMovement->Velocity.X, BoardMovement->Velocity.Y, 0.0f) : FVector::ZeroVector;
	const float NoseSign = (Velocity2D.SizeSquared() > 1.0f && FVector::DotProduct(GetActorForwardVector(), Velocity2D) < 0.0f) ? -1.0f : 1.0f;
	RiderStanceSide = -OldSide;
	RiderFacingYawDeg = FRotator::NormalizeAxis(BoardYawDeg + 90.0f * RiderStanceSide);
	// The offset comes off towards 0 (UpdateRiderPose), so its sign is the way the body turns. Just under
	// 180, so FMath::FixedTurn takes it down the side given rather than the way it picks at exactly 180.
	RiderTurnOffsetDeg = 179.0f * OldSide * NoseSign * (bViaTravelNose ? 1.0f : -1.0f);
	BackToKiteSeconds = 0.0f;
	SetRidingStance(ETrickStance::Heelside);
}

float AKiteRiderPawn::ChooseStanceSide(float BoardYawDeg, float PreferredFacingYawDeg)
{
	// The rider stands across the board, so they face one rail or the other: +1 is the board's right.
	const float OffRightDeg = FMath::Abs(FRotator::NormalizeAxis(PreferredFacingYawDeg - (BoardYawDeg + 90.0f)));
	return OffRightDeg <= 90.0f ? 1.0f : -1.0f;
}

void AKiteRiderPawn::UpdateRiderPose(float DeltaTime)
{
	const float BoardYawDeg = GetActorRotation().Yaw;
	const bool bHasKite = HasKitePosition();
	const bool bAirborne = BoardMovement && BoardMovement->GetBoardState() == EBoardState::Airborne;
	const FVector TowardsKite = bHasKite ? SmoothedKiteOffset.GetSafeNormal2D() : FVector::ZeroVector;
	const FVector KiteNow = bHasKite ? (Kite->GetKiteWorldPosition() - GetActorLocation()).GetSafeNormal2D() : FVector::ZeroVector;

	// The rider's feet are in the straps, so they turn with the board: through carves, and through
	// every spin in the air. They pick which rail to face when they get on (start, reset, or
	// floating in the water), and then face the kite. After that the side that keeps them facing
	// the way they were is kept, which also covers a twin-tip swapping ends under them.
	if (BoardMovement && BoardMovement->GetResetCount() != SeenBoardResetCount)
	{
		// Back on the board from scratch: face the kite again.
		SeenBoardResetCount = BoardMovement->GetResetCount();
		StanceChoiceSecondsLeft = StanceChoiceWindowSeconds;
		RiderTurnOffsetDeg = 0.0f;
	}
	else
	{
		StanceChoiceSecondsLeft = FMath::Max(StanceChoiceSecondsLeft - DeltaTime, 0.0f);
	}
	const bool bCanChooseSide = StanceChoiceSecondsLeft > 0.0f || (BoardMovement && BoardMovement->IsFloating());
	if (bCanChooseSide && bHasKite)
	{
		RiderStanceSide = ChooseStanceSide(BoardYawDeg, KiteNow.Rotation().Yaw);
		BackToKiteSeconds = 0.0f;
	}
	else
	{
		// Keep the side that keeps the rider facing the way they were. In the air with the attitude,
		// that is the way the body faces now, so the riding pose after the landing is the body's.
		float PreferredFacingYawDeg = RiderFacingYawDeg;
		if (RiderAttitude && RiderAttitude->IsSimulating())
		{
			const FVector BodyFront = RiderAttitude->GetRenderBodyQuat(LastRenderAlpha).GetAxisX();
			if (BodyFront.Size2D() > 0.3f)
			{
				PreferredFacingYawDeg = FMath::RadiansToDegrees(FMath::Atan2(BodyFront.Y, BodyFront.X));
			}
		}
		RiderStanceSide = ChooseStanceSide(BoardYawDeg, PreferredFacingYawDeg);

		// The harness hook is on the rider's front, so on the water they do not ride with their
		// back to the kite: after a moment of that they slide the board round under them and
		// face it again. In the air they are free to spin.
		const FVector StanceFacing = FRotator(0.0f, BoardYawDeg + 90.0f * RiderStanceSide, 0.0f).Vector();
		const bool bBackToKite = bHasKite && !bAirborne && FVector::DotProduct(StanceFacing, KiteNow) < -0.25f;
		// A toeside or blind landing (T3.5) is held instead: StepStance ends it with the same slide round.
		BackToKiteSeconds = bBackToKite ? BackToKiteSeconds + DeltaTime : 0.0f;
		if (BackToKiteSeconds > RiderSwitchDelaySeconds && RidingStance == ETrickStance::Heelside)
		{
			RiderStanceSide = -RiderStanceSide;
			RiderTurnOffsetDeg = FRotator::NormalizeAxis(RiderTurnOffsetDeg + 180.0f);
			BackToKiteSeconds = 0.0f;
		}
	}
	RiderFacingYawDeg = FRotator::NormalizeAxis(BoardYawDeg + 90.0f * RiderStanceSide);

	// The slide round is quick but not instant.
	RiderTurnOffsetDeg = FMath::FixedTurn(RiderTurnOffsetDeg, 0.0f, RiderSwitchTurnRateDeg * DeltaTime);
	const FVector Facing = FRotator(0.0f, RiderFacingYawDeg + RiderTurnOffsetDeg, 0.0f).Vector();

	// In the air the hang from the harness is the kinematic stand-in for the rotation the attitude
	// simulates: with the attitude live the riding pose keeps its water lean, so the hand-over at the
	// take-off starts from the pose the rider left the water in.
	// The load is drawn letting go no faster than the board lets it go on the water: the pop zeroes
	// the board's load at once, and the rider stands up out of the crouch over a moment instead of
	// the pelvis jumping 30 cm in a frame. Otherwise it is the board's load as it always was.
	const float BoardLoad = BoardMovement ? BoardMovement->GetLoadAmount() : 0.0f;
	const float LoadReleaseRate = BoardMovement ? FMath::Max(BoardMovement->LoadReleaseRatePerSec, 0.1f) : 6.0f;
	DrawnLoad = bViewInitialized ? FMath::Max(BoardLoad, DrawnLoad - LoadReleaseRate * DeltaTime) : BoardLoad;
	const float Load = DrawnLoad;
	const bool bAttitudeLive = RiderAttitude && RiderAttitude->IsSimulating();
	// Holding the carve against the harness's limit leans the rider back against the hook, applied
	// like the load (the board lets it go gradually, so it needs no smoothing of its own).
	const float HarnessLean = BoardMovement ? BoardMovement->GetHarnessLeanAmount() : 0.0f;
	const FVector BodyUp = ComputeLevelBodyUp(Facing, TowardsKite, bHasKite, bAirborne && !bAttitudeLive, Load, HarnessLean);
	const FQuat BodyQuat = FRotationMatrix::MakeFromXZ(Facing, BodyUp.GetSafeNormal()).ToQuat();

	// The jointed rider: feet in the straps wherever the board goes, pelvis over them along the
	// body's lean and lower in a crouch, knees bending to fit.
	FRiderRigInput RigInput;
	// The feet are in the straps of the board that is drawn, which may be turned away from the root.
	RigInput.Board = BoardVisual ? BoardVisual->GetComponentTransform() : GetActorTransform();
	RigInput.Facing = Facing;
	RigInput.BodyUp = BodyUp.GetSafeNormal();
	// The crouch: the load, or the tuck in the air.
	RigInput.Crouch = FMath::Max(Load, RiderAttitude ? RiderAttitude->GetTuckAmount() : 0.0f);
	// Riding toeside (T3.5) the hips face away from the kite and the torso twists back towards it over
	// them, to the side the kite is on as the twist starts; only on the water, gone in the air.
	{
		float TwistTarget = 0.0f;
		if (RidingStance == ETrickStance::Toeside && !bAirborne && bHasKite)
		{
			if (FMath::Abs(DrawnTorsoTwistDeg) < 1.0f)
			{
				const FVector RightOfFacing = FVector::CrossProduct(FVector::UpVector, Facing);
				TorsoTwistSign = FVector::DotProduct(KiteNow, RightOfFacing) >= 0.0f ? 1.0f : -1.0f;
			}
			TwistTarget = ToesideTorsoTwistDeg * TorsoTwistSign;
		}
		DrawnTorsoTwistDeg = bViewInitialized ? FMath::FInterpConstantTo(DrawnTorsoTwistDeg, TwistTarget, DeltaTime, TorsoTwistRateDegPerSec) : TwistTarget;
		RigInput.TorsoTwistDeg = DrawnTorsoTwistDeg * (1.0f - FMath::SmoothStep(0.0f, 1.0f, RiderAirBlend));
	}
	// On the water RigInput.BodyQuat stays unset and the rig solves from the level Facing and BodyUp.
	// In the air it is the rider attitude's body. Over RiderHandoverSeconds from the take-off, and
	// back after the landing, the torso turns between the two and the pelvis line with it, so the
	// pelvis starts and ends exactly where the riding pose has it.
	if (RiderAirBlend > 0.0f)
	{
		const FQuat AirBody = bAttitudeLive ? RiderAttitude->GetRenderBodyQuat(LastRenderAlpha) : LastAirBodyQuat;
		const float W = FMath::SmoothStep(0.0f, 1.0f, RiderAirBlend);
		const FVector LevelUp = BodyUp.GetSafeNormal().IsNearlyZero() ? FVector::UpVector : BodyUp.GetSafeNormal();
		RigInput.BodyQuat = FQuat::Slerp(BodyQuat, AirBody, W).GetNormalized();
		RigInput.PelvisUp = FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(LevelUp, AirBody.GetAxisZ()), W).RotateVector(LevelUp);
	}
	RiderPose = RiderRig::SolveBody(RigInput);

	// Grabs and the one-footer (T2.1, T2.2), drawn between the last two steps. The nose is on the side
	// of the body the drawn board's +X points to, and the front hand and foot are on that side.
	// The board-off (T2.3) takes over the hands and the feet from the grab and the one-footer.
	const FBoardOffState& BoardOff = GrabState.GetBoardOff();
	const float BoardOffWeight = FMath::Lerp(BoardOff.GetPrevOffWeight(), BoardOff.GetOffWeight(), LastRenderAlpha);
	const bool bBoardOffDrawn = BoardOffWeight > 0.0f;
	if (BoardOff.GetVariant() != ETrickBoardOff::None)
	{
		// Kept for the last frames of a catch, when the state is already back on the feet.
		DrawnBoardOffVariant = BoardOff.GetVariant();
	}
	const float GrabWeight = FMath::Lerp(GrabState.GetPrevReachWeight(), GrabState.GetReachWeight(), LastRenderAlpha);
	const float FootWeight = bBoardOffDrawn ? 0.0f : FMath::SmoothStep(0.0f, 1.0f, FMath::Lerp(GrabState.GetPrevFootOut(), GrabState.GetFootOut(), LastRenderAlpha));
	const float NoseSideSign = FVector::DotProduct(RigInput.Board.GetUnitAxis(EAxis::X), RiderPose.Hips.GetAxisY()) >= 0.0f ? 1.0f : -1.0f;
	const int32 FrontRigSide = NoseSideSign > 0.0f ? 1 : 0;
	const bool bGrabDrawn = !bBoardOffDrawn && GrabState.IsHandOffBar() && GrabWeight > 0.0f;
	const ETrickHand GrabHand = GrabState.GetHand();
	const ETrickGrabZone GrabZone = GrabState.GetZone();
	const int32 GrabRigSide = GrabHand == ETrickHand::Front ? FrontRigSide : 1 - FrontRigSide;
	GrabBoardPullCm = FVector::ZeroVector;
	if (bGrabDrawn || FootWeight > 0.0f)
	{
		FRiderRigInput TrickInput = RigInput;
		if (bGrabDrawn)
		{
			// The body holds still (the pelvis where the riding pose put it), folds at the hips towards
			// the zone, and the drawn board comes up towards the grabbing hand's shoulder until the socket
			// is within GrabReachFraction of the arm's reach, as far as GrabMaxBoardPullCm. Only the drawn
			// board moves: the physics root does not. The board can only be moved while the attitude
			// draws it (in the air and through the landing's hand-over).
			TrickInput.PelvisAnchor = RiderPose.Pelvis;
			TrickInput.TorsoPitchDeg = GrabWeight * FGrabState::TorsoFoldDegForZone(GrabZone);
			FRiderRigPose Folded = RiderPose;
			Folded.Torso = (RiderPose.Torso * FQuat(FVector::YAxisVector, FMath::DegreesToRadians(TrickInput.TorsoPitchDeg))).GetNormalized();
			const FVector Shoulder = RiderRig::ShoulderPosition(Folded, GrabRigSide);
			const FVector Socket = BoardGrabPoints::SocketWorld(RigInput.Board, GrabZone, GrabHand, NoseSideSign);
			const float WantedCm = (RiderRig::UpperArmLengthCm + RiderRig::ForearmLengthCm) * GrabReachFraction;
			const float DistanceCm = static_cast<float>(FVector::Dist(Shoulder, Socket));
			if (bAttitudeOwnsBoardVisual && BoardVisual && DistanceCm > WantedCm)
			{
				GrabBoardPullCm = (Shoulder - Socket).GetSafeNormal() * (FMath::Min(DistanceCm - WantedCm, GrabMaxBoardPullCm) * GrabWeight);
				TrickInput.Board.AddToTranslation(GrabBoardPullCm);
				BoardVisual->SetWorldLocation(BoardVisual->GetComponentLocation() + GrabBoardPullCm);
			}
			TrickInput.Hands[GrabRigSide].Target = ERiderHandTarget::BoardSocket;
			TrickInput.Hands[GrabRigSide].BoardSocket = BoardGrabPoints::SocketFor(GrabZone, GrabHand, NoseSideSign);
		}
		if (FootWeight > 0.0f)
		{
			// The back foot kicks out of its strap off the tail, on the heel side.
			const int32 BackFootSide = 1 - FrontRigSide;
			const FVector Strap = TrickInput.Board.TransformPosition(BoardGrabPoints::ToBoardLocal(BoardGrabPoints::BackStrap, NoseSideSign));
			const FVector Kicked = TrickInput.Board.TransformPosition(BoardGrabPoints::ToBoardLocal(OneFootKickStanceCm, NoseSideSign));
			TrickInput.Feet[BackFootSide].AnkleTarget = FMath::Lerp(Strap, Kicked, FootWeight);
		}
		RigInput = TrickInput;
		RiderPose = RiderRig::SolveBody(RigInput);
	}
	FBoardOffPose BoardOffDrawnPose;
	FTransform BoardOffDrawnBoard = RigInput.Board;
	if (bBoardOffDrawn)
	{
		// The board-off, drawn between the last two steps: the held board in the rider frame (the
		// pelvis and the body as the riding pose put them), blended from the strapped board by the
		// eased removal or catch. The body holds still (the pelvis anchored) and folds at the hips as
		// the variant asks; the feet leave the straps for the variant's ankles. Only the drawn board
		// moves, and only while the attitude draws it; the physics root does not.
		const float TicTacDeg = FMath::Lerp(BoardOff.GetPrevTicTacDeg(), BoardOff.GetTicTacDeg(), LastRenderAlpha);
		const float PassPhase = FMath::Lerp(BoardOff.GetPrevPassPhase(), BoardOff.GetPassPhase(), LastRenderAlpha);
		BoardOffDrawnPose = BoardOffPose::Evaluate(DrawnBoardOffVariant, TicTacDeg, PassPhase, NoseSideSign);
		const FTransform Frame = BoardOffPose::RiderFrame(RiderPose.Pelvis, RiderPose.Torso);
		if (bAttitudeOwnsBoardVisual && BoardVisual)
		{
			BoardOffDrawnBoard = BoardOffPose::BlendBoard(RigInput.Board, BoardOffDrawnPose.BoardInRider * Frame, BoardOffWeight);
			SetBoardVisualWorldRotation(BoardOffDrawnBoard.GetRotation());
			BoardVisual->SetWorldLocation(BoardOffDrawnBoard.GetLocation());
		}
		FRiderRigInput TrickInput = RigInput;
		TrickInput.Board = BoardOffDrawnBoard;
		TrickInput.PelvisAnchor = RiderPose.Pelvis;
		TrickInput.TorsoPitchDeg = BoardOffWeight * BoardOffDrawnPose.TorsoPitchDeg;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			// Out of the straps of the board as it is drawn (so they leave it, and come back to it, smoothly).
			const FBoardStancePointCm& StrapPoint = Side == FrontRigSide ? BoardGrabPoints::FrontStrap : BoardGrabPoints::BackStrap;
			const FVector Strap = BoardOffDrawnBoard.TransformPosition(BoardGrabPoints::ToBoardLocal(StrapPoint, NoseSideSign));
			TrickInput.Feet[Side].AnkleTarget = FMath::Lerp(Strap, Frame.TransformPosition(BoardOffDrawnPose.AnkleInRider[Side]), BoardOffWeight);
		}
		RigInput = TrickInput;
		RiderPose = RiderRig::SolveBody(RigInput);
	}
	// The bar hangs in front of the body the rig drew: the level facing on the water, the body's own in the air.
	const bool bBodyPose = RigInput.BodyQuat.IsSet();
	const FVector BarFacing = bBodyPose ? RiderPose.Torso.GetAxisX() : Facing;

	// The lines pull on the harness hook at the front of the rider's waist. The bar rides on them
	// just beyond the hook, further out the more it is sheeted out, and always in front of the
	// body: when the kite is behind a spinning rider the lines come over their shoulder.
	HarnessHookPosition = RiderPose.Pelvis + RiderPose.Torso.RotateVector(HarnessHookOffsetCm);
	if (Kite)
	{
		const float HandSpacingCm = 14.0f;
		FVector LineDir;
		FVector BarCentre;
		FVector TiltedSpan;
		// Which hands hold the bar (rig side 0 left, 1 right) and where a free hand goes.
		bool bHandOnBar[2] = { true, true };
		FVector FreeHand[2] = { FVector::ZeroVector, FVector::ZeroVector };
		bool bHandsBehind = false;
		if (Bar.bHooked)
		{
			LineDir = bHasKite ? (Kite->GetKiteWorldPosition() - HarnessHookPosition).GetSafeNormal() : BarFacing;
			const float MinForward = 0.15f;
			const float Forward = FVector::DotProduct(LineDir, BarFacing);
			if (Forward < MinForward)
			{
				LineDir = (LineDir + BarFacing * (MinForward - Forward)).GetSafeNormal();
			}
			// The bar is never further up the lines than the rider's arms reach: leaning back with the
			// kite low, it comes in closer to the hook.
			const float ArmReachCm = (RiderRig::UpperArmLengthCm + RiderRig::ForearmLengthCm) * 0.97f;
			float BarReachCm = 42.0f + 25.0f * (1.0f - CurrentSheetInput);
			const float MinBarReachCm = 14.0f;
			for (int32 Try = 0; Try < 16 && BarReachCm > MinBarReachCm; ++Try)
			{
				const FVector Candidate = HarnessHookPosition + LineDir * BarReachCm;
				// The hands are either side of the bar's middle; to judge reach, level with the shoulders is near enough.
				const FVector Across = RiderPose.Torso.GetAxisY() * HandSpacingCm;
				const float Furthest = FMath::Max(FVector::Dist(RiderPose.Arms[0].Root, Candidate - Across), FVector::Dist(RiderPose.Arms[1].Root, Candidate + Across));
				if (Furthest <= ArmReachCm)
				{
					break;
				}
				BarReachCm = FMath::Max(BarReachCm - 4.0f, MinBarReachCm);
			}
			BarCentre = HarnessHookPosition + LineDir * BarReachCm;
		}
		else
		{
			// Unhooked (docs/tricks/T3.md 1.4): the bar is where the physics pulls (LineAttach), in the
			// body the rig drew: in front at the arms' extension, behind the back, on the pass arc, or on
			// the leash up the lines from the harness.
			const FQuat Torso = RiderPose.Torso;
			const FVector Com = RiderPose.Pelvis + Torso.RotateVector(FVector(0.0f, 0.0f, 10.0f));
			LineDir = bHasKite ? (Kite->GetKiteWorldPosition() - Com).GetSafeNormal() : Torso.GetAxisX();
			const float Nose = GetBarNoseSideSign();
			const FVector Attach = LineAttach::AttachPointBody(Bar, Torso.UnrotateVector(LineDir), GetArmExtension(), Nose, LineAttachTunables);
			switch (Bar.Place)
			{
			case EBarPlace::Lost:
				BarCentre = HarnessHookPosition + LineDir * LeashLengthCm;
				bHandOnBar[0] = bHandOnBar[1] = false;
				Kite->SetLeashAnchor(HarnessHookPosition);
				break;
			case EBarPlace::Passing:
			{
				BarCentre = Com + Torso.RotateVector(Attach);
				// The giving hand lets go past 0.6 of the way round, the other takes it from 0.4.
				const int32 GivingSide = LineAttach::PassGivingSide(Bar, Nose) > 0.0f ? 1 : 0;
				bHandOnBar[GivingSide] = Bar.PassT <= 0.6f;
				bHandOnBar[1 - GivingSide] = Bar.PassT >= 0.4f;
				bHandsBehind = true;
				break;
			}
			case EBarPlace::BehindBack:
				BarCentre = Com + Torso.RotateVector(Attach);
				bHandsBehind = true;
				break;
			default:
				BarCentre = Com + Torso.RotateVector(Attach);
				bHandOnBar[0] = Bar.Hands != (Nose > 0.0f ? EBarHands::FrontOnly : EBarHands::BackOnly);
				bHandOnBar[1] = Bar.Hands != (Nose > 0.0f ? EBarHands::BackOnly : EBarHands::FrontOnly);
				break;
			}
			for (int32 Side = 0; Side < 2; ++Side)
			{
				// A hand off the bar hangs out to its side at the hip.
				FreeHand[Side] = RiderPose.Pelvis + Torso.RotateVector(FVector(15.0f, Side == 0 ? -40.0f : 40.0f, 5.0f));
			}
		}
		DrawnBarCentre = BarCentre;

		// The bar is held square to the lines across the rider's body, and tilts with the steering;
		// behind the back it lies across the back.
		const FVector RiderRight = bBodyPose ? RiderPose.Torso.GetAxisY() : FVector::CrossProduct(FVector::UpVector, Facing);
		FVector Span = (RiderRight - FVector::DotProduct(RiderRight, LineDir) * LineDir).GetSafeNormal();
		if (Span.IsNearlyZero() || bHandsBehind)
		{
			Span = RiderRight;
		}
		const float TiltRad = FMath::DegreesToRadians((bHandsBehind ? 0.0f : CurrentSteerInput) * 25.0f);
		TiltedSpan = Span * FMath::Cos(TiltRad) + FVector::CrossProduct(LineDir, Span) * FMath::Sin(TiltRad);
		const float BarHalfWidthCm = 25.0f;
		Kite->SetBarEnds(BarCentre - TiltedSpan * BarHalfWidthCm, BarCentre + TiltedSpan * BarHalfWidthCm);
		if (ControlBarMesh)
		{
			ControlBarMesh->SetWorldLocationAndRotation(BarCentre, FRotationMatrix::MakeFromXY(LineDir, TiltedSpan).ToQuat());
		}
		if (!Bar.bHooked)
		{
			// Unhooked hands: one hand alone holds the bar's middle; a hand off the bar goes free; behind
			// the back the elbows point out and back.
			for (int32 Side = 0; Side < 2; ++Side)
			{
				FRiderHandInput& Hand = RigInput.Hands[Side];
				if (bGrabDrawn && Side == GrabRigSide)
				{
					// A one-hand grab (T3.3): the hand is on its way to the board or on it, set above and below.
					continue;
				}
				if (!bHandOnBar[Side])
				{
					Hand.Target = ERiderHandTarget::Free;
					Hand.WorldTarget = FreeHand[Side];
				}
				else if (!bHandOnBar[1 - Side])
				{
					Hand.Target = ERiderHandTarget::Free;
					Hand.WorldTarget = BarCentre;
				}
				if (bHandsBehind)
				{
					Hand.ElbowPole = RiderRig::BehindBackElbowPole(RiderPose, Side);
				}
			}
		}

		// Hands on the bar, either side of its middle; the elbows bend to reach. A grabbing hand goes to
		// its socket on the drawn board, from the bar over the reach (T2.1); the other stays on the bar.
		const FVector BarHands[2] = { BarCentre - TiltedSpan * HandSpacingCm, BarCentre + TiltedSpan * HandSpacingCm };
		if (bGrabDrawn && GrabWeight < 1.0f)
		{
			FRiderHandInput& GrabbingHand = RigInput.Hands[GrabRigSide];
			const FVector Socket = RiderRig::HandTarget(RiderPose, RigInput, GrabRigSide, BarHands[GrabRigSide]);
			const FVector Pole = FMath::Lerp(RiderRig::DefaultElbowPole(RiderPose, GrabRigSide), RiderRig::GrabElbowPole(RiderPose, GrabRigSide), GrabWeight);
			GrabbingHand.Target = ERiderHandTarget::Free;
			GrabbingHand.WorldTarget = FMath::Lerp(BarHands[GrabRigSide], Socket, GrabWeight);
			GrabbingHand.ElbowPole = Pole;
		}
		if (bBoardOffDrawn)
		{
			// The board-off's hands go from the bar to their grips on the drawn board as it comes off,
			// and back as it is caught; a hand the variant leaves on the bar stays there.
			for (int32 HandIndex = 0; HandIndex < 2; ++HandIndex)
			{
				const float W = BoardOffWeight * BoardOffDrawnPose.HandOnBoard[HandIndex];
				if (W <= 0.0f)
				{
					continue;
				}
				const int32 Side = HandIndex == static_cast<int32>(ETrickHand::Front) ? FrontRigSide : 1 - FrontRigSide;
				const FVector Grip = BoardOffDrawnBoard.TransformPosition(BoardOffDrawnPose.GripLocal[HandIndex]);
				const FVector OnBoardPole = FMath::Lerp(RiderRig::GrabElbowPole(RiderPose, Side), RiderRig::BehindBackElbowPole(RiderPose, Side), BoardOffDrawnPose.HandBehind[HandIndex]);
				FRiderHandInput& BoardHand = RigInput.Hands[Side];
				BoardHand.Target = ERiderHandTarget::Free;
				BoardHand.WorldTarget = FMath::Lerp(BarHands[Side], Grip, W);
				BoardHand.ElbowPole = FMath::Lerp(RiderRig::DefaultElbowPole(RiderPose, Side), OnBoardPole, W);
			}
		}
		RiderRig::SolveArmsPerHand(RiderPose, RigInput, BarHands[0], BarHands[1]);
	}

	// Draw the parts where the rig put them.
	if (RiderTorso)
	{
		RiderTorso->SetWorldLocationAndRotation(RiderPose.Pelvis, RiderPose.Torso);
	}
	if (RiderLimbs.Num() == 8)
	{
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FRiderLimbPose& Leg = RiderPose.Legs[Side];
			const FRiderLimbPose& Arm = RiderPose.Arms[Side];
			const FTransform Parts[4] =
			{
				RiderRig::SegmentTransform(Leg.Root, Leg.Joint, Leg.Pole),
				// The foot points the way the knee does.
				RiderRig::SegmentTransform(Leg.Joint, Leg.End, Leg.Pole),
				RiderRig::SegmentTransform(Arm.Root, Arm.Joint, Arm.Pole),
				RiderRig::SegmentTransform(Arm.Joint, Arm.End, Arm.Pole),
			};
			for (int32 Part = 0; Part < 4; ++Part)
			{
				if (UStaticMeshComponent* Component = RiderLimbs[Side * 4 + Part])
				{
					Component->SetWorldLocationAndRotation(Parts[Part].GetLocation(), Parts[Part].GetRotation());
				}
			}
		}
	}
}

void AKiteRiderPawn::SetBoardVisualWorldRotation(const FQuat& WorldRotation)
{
	if (!BoardVisual)
	{
		return;
	}
	// The rotation is held in world space, so it stays put however the root turns; the location
	// stays relative, so the board goes where the body goes.
	bBoardVisualOverride = true;
	BoardVisual->SetUsingAbsoluteRotation(true);
	BoardVisual->SetWorldRotation(WorldRotation.GetNormalized());
}

void AKiteRiderPawn::ClearBoardVisualOverride()
{
	bBoardVisualOverride = false;
	if (BoardVisual)
	{
		BoardVisual->SetUsingAbsoluteRotation(false);
		BoardVisual->SetRelativeRotation(FQuat::Identity);
	}
}

namespace KiteCamera
{
	/** Something the camera must keep in frame: a point and how much room it needs round it (cm). */
	struct FFrameTarget
	{
		FVector Location;
		float RadiusCm;
	};

	/**
	 * How the targets must sit in the frame: the screen's width over its height, the room kept round
	 * each target (deg), and how far out from the centre they may go, as fractions of the half frame
	 * to the side, to the top and to the bottom.
	 */
	struct FFrameRules
	{
		float Aspect = 16.0f / 9.0f;
		float MarginDeg = 0.0f;
		float Side = 1.0f;
		float Top = 1.0f;
		float Bottom = 1.0f;
	};

	/** Above this a target cannot be framed at all (the tangent of an 85 deg half field of view). */
	constexpr float UnframeableTan = 11.43f;

	/**
	 * The tangent of the smallest horizontal half field of view that shows every target, with its
	 * radius and the rules' margin round it and inside the rules' fractions of the frame, from a
	 * level camera at CameraLocation looking along Yaw and Pitch. UnframeableTan if one is behind
	 * the camera.
	 */
	float RequiredTanHalfFov(const FVector& CameraLocation, float YawDeg, float PitchDeg, TConstArrayView<FFrameTarget> Targets, const FFrameRules& Rules)
	{
		const FQuat View = FRotator(PitchDeg, YawDeg, 0.0f).Quaternion();
		float Required = 0.0f;
		for (const FFrameTarget& Target : Targets)
		{
			const FVector Local = View.UnrotateVector(Target.Location - CameraLocation);
			const float Distance = Local.Size();
			if (Distance < 1.0f)
			{
				continue;
			}
			if (Local.X <= 1.0f)
			{
				return UnframeableTan;
			}
			const float PadDeg = Rules.MarginDeg + FMath::RadiansToDegrees(FMath::Asin(FMath::Min(Target.RadiusCm / Distance, 0.5f)));
			const float AcrossDeg = FMath::RadiansToDegrees(FMath::Atan2(FMath::Abs(Local.Y), Local.X)) + PadDeg;
			const float UpDownDeg = FMath::RadiansToDegrees(FMath::Atan2(FMath::Abs(Local.Z), Local.X)) + PadDeg;
			if (AcrossDeg >= 85.0f || UpDownDeg >= 85.0f)
			{
				return UnframeableTan;
			}
			const float UpDownFraction = Local.Z >= 0.0f ? Rules.Top : Rules.Bottom;
			Required = FMath::Max3(Required, FMath::Tan(FMath::DegreesToRadians(AcrossDeg)) / Rules.Side,
				FMath::Tan(FMath::DegreesToRadians(UpDownDeg)) * Rules.Aspect / UpDownFraction);
		}
		return FMath::Min(Required, UnframeableTan);
	}

	/**
	 * The look pitch between MinPitch and MaxPitch nearest Preferred at which every target fits a
	 * horizontal half field of view whose tangent is TanBudget; if none does, the pitch that needs
	 * the narrowest view. OutRequiredTan is what the returned pitch needs.
	 */
	float SolvePitch(const FVector& CameraLocation, float YawDeg, TConstArrayView<FFrameTarget> Targets, const FFrameRules& Rules,
		float PreferredDeg, float MinPitchDeg, float MaxPitchDeg, float TanBudget, float& OutRequiredTan)
	{
		constexpr float StepDeg = 0.5f;
		float BestFitDeg = 0.0f;
		float BestFitRequired = 0.0f;
		bool bFound = false;
		float NarrowestDeg = PreferredDeg;
		float NarrowestRequired = TNumericLimits<float>::Max();
		const int32 Steps = FMath::Max(FMath::CeilToInt((MaxPitchDeg - MinPitchDeg) / StepDeg), 0);
		// The preferred pitch itself first, so a shot that already works is kept exactly.
		for (int32 Index = -1; Index <= Steps; ++Index)
		{
			const float PitchDeg = Index < 0 ? FMath::Clamp(PreferredDeg, MinPitchDeg, MaxPitchDeg) : FMath::Min(MinPitchDeg + Index * StepDeg, MaxPitchDeg);
			const float Required = RequiredTanHalfFov(CameraLocation, YawDeg, PitchDeg, Targets, Rules);
			if (Required < NarrowestRequired)
			{
				NarrowestRequired = Required;
				NarrowestDeg = PitchDeg;
			}
			if (Required <= TanBudget && (!bFound || FMath::Abs(PitchDeg - PreferredDeg) < FMath::Abs(BestFitDeg - PreferredDeg)))
			{
				bFound = true;
				BestFitDeg = PitchDeg;
				BestFitRequired = Required;
			}
		}
		OutRequiredTan = bFound ? BestFitRequired : NarrowestRequired;
		return bFound ? BestFitDeg : NarrowestDeg;
	}

	float TanHalf(float FovDeg) { return FMath::Tan(FMath::DegreesToRadians(FovDeg * 0.5f)); }
	float FovFromTanHalf(float Tan) { return 2.0f * FMath::RadiansToDegrees(FMath::Atan(Tan)); }

	/** The view's width over its height: the game viewport's when there is one, else 16:9. */
	float ViewAspect(const UWorld* World)
	{
		if (const UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr)
		{
			FVector2D Size;
			Viewport->GetViewportSize(Size);
			if (Size.X > 0.0 && Size.Y > 0.0)
			{
				return FMath::Clamp(static_cast<float>(Size.X / Size.Y), 1.0f, 4.0f);
			}
		}
		return 16.0f / 9.0f;
	}
}

void AKiteRiderPawn::UpdateCamera(float DeltaTime)
{
	using namespace KiteCamera;
	if (!CameraBoom || !FollowCamera)
	{
		return;
	}

	const bool bAirborne = BoardMovement && BoardMovement->GetBoardState() == EBoardState::Airborne;

	// The boom pivot. On the water it rides on the board, tilting with the swell and the edge as it
	// always has. In the air it sits straight above the board, so a board pitched or rolled by a
	// trick does not swing the camera about. Between the two it blends over CameraPivotLevelSeconds.
	const float PivotBlendTarget = bAirborne ? 1.0f : 0.0f;
	CameraPivotAirBlend = (bViewInitialized && CameraPivotLevelSeconds > 0.0f)
		? FMath::FInterpConstantTo(CameraPivotAirBlend, PivotBlendTarget, DeltaTime, 1.0f / CameraPivotLevelSeconds)
		: PivotBlendTarget;
	const FVector PivotOffset(0.0f, 0.0f, CameraPivotHeightCm);
	if (CameraPivotAirBlend <= 0.0f)
	{
		CameraBoom->SetRelativeLocation(PivotOffset);
	}
	else
	{
		const FTransform& RootTransform = GetActorTransform();
		const FVector WorldOffset = FMath::Lerp(RootTransform.TransformVectorNoScale(PivotOffset), PivotOffset, CameraPivotAirBlend);
		CameraBoom->SetRelativeLocation(RootTransform.InverseTransformVectorNoScale(WorldOffset));
	}
	const FVector Pivot = CameraBoom->GetComponentLocation();

	// Where the boom's location lag will leave the camera this frame, relative to where it would be
	// without lag. The camera moved with the pivot since the boom last placed it, so what is left
	// over is the lag then; the boom closes part of the gap and caps it at CameraLagMaxDistance.
	FVector PredictedLag = FVector::ZeroVector;
	if (bViewInitialized && CameraBoom->bEnableCameraLag)
	{
		const FVector UnlaggedCamera = Pivot - CameraBoom->GetComponentRotation().Vector() * CameraBoom->TargetArmLength;
		const FVector LastLag = FollowCamera->GetComponentLocation() - UnlaggedCamera;
		const float Closed = CameraBoom->CameraLagSpeed > 0.0f ? FMath::Clamp(DeltaTime * CameraBoom->CameraLagSpeed, 0.0f, 1.0f) : 1.0f;
		PredictedLag = (LastLag - (Pivot - CameraLastPivotLocation)) * (1.0f - Closed);
		if (CameraBoom->CameraLagMaxDistance > 0.0f)
		{
			PredictedLag = PredictedLag.GetClampedToMaxSize(CameraBoom->CameraLagMaxDistance);
		}
	}
	CameraLastPivotLocation = Pivot;

	// The heading the camera looks along. On the water it is the board's: switch stance keeps the
	// nose forward, so that is the way the rider is going. In the air the board spins, so it is the
	// flight's horizontal direction, held while that is too slow to mean anything.
	float HeadingYawDeg = GetActorRotation().Yaw;
	if (bAirborne)
	{
		const FVector Velocity = GetBoardVelocity();
		if (Velocity.SizeSquared2D() > FMath::Square(CameraAirMinSpeedCmS))
		{
			HeadingYawDeg = FMath::RadiansToDegrees(FMath::Atan2(Velocity.Y, Velocity.X));
		}
		else if (bViewInitialized)
		{
			HeadingYawDeg = CameraHeadingYawDeg;
		}
	}
	CameraHeadingYawDeg = HeadingYawDeg;
	float TargetYawDeg = HeadingYawDeg;

	const bool bHasKite = HasKitePosition();
	const FVector SmoothedKitePosition = GetActorLocation() + SmoothedKiteOffset;
	if (bHasKite)
	{
		// Look along the heading, but never further from the kite than the offset that keeps it in
		// frame. Near the zenith the kite's direction across the water means little and flips as it
		// goes over, so the clamp lets go there: any heading has an overhead kite in frame.
		const FVector PivotToKite = SmoothedKitePosition - Pivot;
		const float KiteYawDeg = PivotToKite.Rotation().Yaw;
		const float KiteElevationDeg = FMath::RadiansToDegrees(FMath::Atan2(PivotToKite.Z, PivotToKite.Size2D()));
		const float YawLimitDeg = FMath::Lerp(CameraMaxKiteYawOffsetDeg, 180.0f, FMath::SmoothStep(60.0f, 85.0f, KiteElevationDeg));
		const float HeadingFromKiteDeg = FMath::FindDeltaAngleDegrees(KiteYawDeg, HeadingYawDeg);
		TargetYawDeg = KiteYawDeg + FMath::Clamp(HeadingFromKiteDeg, -YawLimitDeg, YawLimitDeg);
	}
	if (bViewInitialized)
	{
		const float YawStepDeg = FMath::FindDeltaAngleDegrees(CameraYawDeg, TargetYawDeg) * FMath::Clamp(CameraTurnSpeed * DeltaTime, 0.0f, 1.0f);
		CameraYawDeg = FRotator::NormalizeAxis(CameraYawDeg + YawStepDeg);
	}
	else
	{
		CameraYawDeg = TargetYawDeg;
	}

	// What must stay in frame: the kite where it really is (a loop swings it far from the smoothed
	// position the shot is composed on) and the rider, from the board to the head.
	TArray<FFrameTarget, TInlineAllocator<3>> Targets;
	Targets.Add({ GetActorLocation(), 60.0f });
	Targets.Add({ GetActorLocation() + GetActorUpVector() * 170.0f, 50.0f });
	if (bHasKite)
	{
		Targets.Add({ Kite->GetKiteWorldPosition(), CameraKiteFrameRadiusCm * Kite->GetSizeScale() });
	}
	FFrameRules FullFrame;
	FullFrame.Aspect = ViewAspect(GetWorld());
	FullFrame.MarginDeg = CameraFrameMarginDeg;
	const float BaseFovDeg = FMath::Min(CameraFOVDeg, CameraMaxFOVDeg);
	const float MaxFramingPitchDeg = FMath::Max(CameraMaxFramingPitchDeg, CameraMaxLookPitchDeg);
	const float MaxArmCm = FMath::Max(CameraMaxArmLengthCm, CameraArmLengthCm);

	// The boom keeps the camera above the water: it tilts further down, lifting the camera, when a
	// lagging camera would otherwise drop towards the surface.
	float WaterHeightCm = 0.0f;
	if (BoardMovement)
	{
		FVector WaterNormal;
		BoardMovement->SampleWaterSurface(Pivot - FRotator(0.0f, CameraYawDeg, 0.0f).Vector() * CameraCurrentArmCm, WaterHeightCm, WaterNormal);
	}
	auto BoomPitchFor = [&](float ArmCm)
	{
		const float LiftNeededCm = WaterHeightCm + CameraMinHeightAboveWaterCm - Pivot.Z - PredictedLag.Z;
		const float SinNeeded = ArmCm > 1.0f ? LiftNeededCm / ArmCm : 0.0f;
		const float NeededPitchDeg = -FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(SinNeeded, -1.0f, FMath::Sin(FMath::DegreesToRadians(45.0f)))));
		return FMath::Min(CameraBoomPitchDeg, NeededPitchDeg);
	};
	auto CameraLocationFor = [&](float ArmCm)
	{
		return Pivot - FRotator(BoomPitchFor(ArmCm), CameraYawDeg, 0.0f).Vector() * ArmCm + PredictedLag;
	};

	// The shot as composed: tilt up only as far as needed to keep a high kite below the top of the
	// screen, at the normal field of view and boom length. If the kite and the rider do not both
	// fit, tilt further up, then widen the view, then pull the boom back.
	float PreferredPitchDeg = CameraMinLookPitchDeg;
	if (bHasKite)
	{
		const FVector CameraToKite = SmoothedKitePosition - CameraLocationFor(CameraCurrentArmCm);
		const float KiteElevationFromCameraDeg = FMath::RadiansToDegrees(FMath::Atan2(CameraToKite.Z, CameraToKite.Size2D()));
		PreferredPitchDeg = FMath::Clamp(KiteElevationFromCameraDeg - CameraKiteHeadroomDeg, CameraMinLookPitchDeg, CameraMaxLookPitchDeg);
	}
	float TargetPitchDeg = PreferredPitchDeg;
	float TargetFovDeg = BaseFovDeg;
	float TargetArmCm = CameraArmLengthCm;
	{
		// The composed shot keeps both well inside the frame, and the kite clear of the HUD along the top.
		FFrameRules Composed = FullFrame;
		Composed.Side = Composed.Bottom = FMath::Clamp(CameraComfortFrameFraction, 0.3f, 1.0f);
		Composed.Top = FMath::Clamp(CameraComfortTopFraction, 0.3f, 1.0f);
		const float BaseTan = TanHalf(BaseFovDeg);
		float RequiredTan = 0.0f;
		TargetPitchDeg = SolvePitch(CameraLocationFor(CameraArmLengthCm), CameraYawDeg, Targets, Composed,
			PreferredPitchDeg, CameraMinLookPitchDeg, MaxFramingPitchDeg, BaseTan, RequiredTan);
		if (RequiredTan > BaseTan)
		{
			// Too much to show at the normal view: widen it, as little as can be, by tilting to the
			// pitch that needs the narrowest view; pull back once widening is not enough.
			const float MaxTan = TanHalf(CameraMaxFOVDeg);
			constexpr float ArmStepCm = 100.0f;
			for (float ArmCm = CameraArmLengthCm; ; ArmCm = FMath::Min(ArmCm + ArmStepCm, MaxArmCm))
			{
				TargetArmCm = ArmCm;
				TargetPitchDeg = SolvePitch(CameraLocationFor(ArmCm), CameraYawDeg, Targets, Composed,
					PreferredPitchDeg, CameraMinLookPitchDeg, MaxFramingPitchDeg, 0.0f, RequiredTan);
				if (RequiredTan <= MaxTan || ArmCm >= MaxArmCm)
				{
					break;
				}
			}
			TargetFovDeg = FMath::Clamp(FovFromTanHalf(RequiredTan), BaseFovDeg, CameraMaxFOVDeg);
		}
	}

	// Ease towards that shot: out quickly when more room is needed, back in slowly so it does not pump.
	if (bViewInitialized)
	{
		CameraLookPitchDeg = FMath::FInterpTo(CameraLookPitchDeg, TargetPitchDeg, DeltaTime, CameraTurnSpeed);
		CameraCurrentArmCm = FMath::FInterpTo(CameraCurrentArmCm, TargetArmCm, DeltaTime, TargetArmCm > CameraCurrentArmCm ? CameraZoomOutSpeed : CameraZoomInSpeed);
		CameraCurrentFOVDeg = FMath::FInterpTo(CameraCurrentFOVDeg, TargetFovDeg, DeltaTime, TargetFovDeg > CameraCurrentFOVDeg ? CameraZoomOutSpeed : CameraZoomInSpeed);
	}
	else
	{
		CameraLookPitchDeg = TargetPitchDeg;
		CameraCurrentArmCm = TargetArmCm;
		CameraCurrentFOVDeg = TargetFovDeg;
	}

	// The easing must never let the kite or the rider out: where the eased shot would lose one,
	// the view widens at once as far as needed, and past CameraMaxFOVDeg the camera tilts too.
	const FVector CameraLocation = CameraLocationFor(CameraCurrentArmCm);
	float RequiredTan = RequiredTanHalfFov(CameraLocation, CameraYawDeg, CameraLookPitchDeg, Targets, FullFrame);
	if (RequiredTan > TanHalf(CameraMaxFOVDeg))
	{
		CameraLookPitchDeg = SolvePitch(CameraLocation, CameraYawDeg, Targets, FullFrame,
			CameraLookPitchDeg, CameraMinLookPitchDeg, MaxFramingPitchDeg, TanHalf(CameraMaxFOVDeg), RequiredTan);
	}
	constexpr float HardMaxFovDeg = 140.0f;
	CameraCurrentFOVDeg = FMath::Clamp(FMath::Max(CameraCurrentFOVDeg, FovFromTanHalf(RequiredTan)), 1.0f, HardMaxFovDeg);
	CameraCurrentBoomPitchDeg = BoomPitchFor(CameraCurrentArmCm);

	// The camera kick (a Stomped landing's punch): eased back to 0, never a new resting state, and
	// added to the look pitch only, so the horizon stays level through it.
	CameraKickDeg = FMath::FInterpTo(CameraKickDeg, 0.0f, DeltaTime, CameraKickDecaySpeed);

	// The boom keeps the camera above the water; the camera itself tilts to look up at the kite.
	// Neither ever rolls, so the horizon stays level.
	CameraBoom->TargetArmLength = CameraCurrentArmCm;
	CameraBoom->SetWorldRotation(FRotator(CameraCurrentBoomPitchDeg, CameraYawDeg, 0.0f));
	FollowCamera->SetRelativeRotation(FRotator(CameraLookPitchDeg - CameraCurrentBoomPitchDeg + CameraKickDeg, 0.0f, 0.0f));
	FollowCamera->SetFieldOfView(CameraCurrentFOVDeg);
}

void AKiteRiderPawn::OnResetTriggered(const FInputActionValue& Value)
{
	ResetRider();
}

void AKiteRiderPawn::ResetRider()
{
	RecentreMotionBar();
	if (BoardMovement)
	{
		BoardMovement->ResetToTack(8.0f);
	}
}

void AKiteRiderPawn::HandleBoardCrash(float Intensity)
{
	// A crash ends the rotation: the rider is back on the riding pose and the drawn board on the root.
	ResetRiderAttitude();
	// Running aground and a shark have just played their own sound.
	if (!bSkipNextCrashSplash)
	{
		PlayOneShot(CrashSound, FMath::Clamp(Intensity, 0.4f, 1.2f));
	}
	bSkipNextCrashSplash = false;
	PlayHaptic(1.0f, 0.45f, true);
}

void AKiteRiderPawn::HandleBoardReset()
{
	ResetRiderAttitude();
	PlayOneShot(ResetSound, 0.5f);
	bSkipNextCrashSplash = false;
}

void AKiteRiderPawn::HandleBoardLanding(float LandingG)
{
	if (LandingSound)
	{
		// A harder landing is a louder, deeper splash.
		const float Hardness = FMath::Clamp((LandingG - 1.0f) / 5.0f, 0.0f, 1.0f);
		PlayOneShot(LandingSound, 0.45f + 0.65f * Hardness, 1.1f - 0.3f * Hardness);
	}
	float HapticIntensity = 0.0f;
	float HapticDuration = 0.0f;
	GetLandingHaptic(LandingG, HapticIntensity, HapticDuration);
	PlayHaptic(HapticIntensity, HapticDuration, true);
}

void AKiteRiderPawn::HandleBoardLandingVerdict(const FLandingVerdict& Verdict)
{
	// On top of HandleBoardLanding's g-scaled splash and buzz (every non-crash landing) and
	// HandleBoardCrash's own thump (a crash, bound to OnBoardCrash, broadcast right after this):
	// Stomped sells the result landing well, Sketchy warns something was off. Clean and Crash add
	// nothing here (review batch D, docs/tricks/review.md section 4).
	switch (Verdict.Grade)
	{
	case ELandingGrade::Stomped:
		PlayHaptic(1.0f, 0.3f, true);
		KickCamera(StompCameraKickDeg);
		PlayOneShot(StompSound, 0.9f);
		break;
	case ELandingGrade::Sketchy:
		PlayHaptic(0.3f, 0.18f, false);
		break;
	case ELandingGrade::Clean:
	case ELandingGrade::Crash:
	default:
		break;
	}
}

void AKiteRiderPawn::KickCamera(float AmountDeg)
{
	CameraKickDeg = FMath::Clamp(CameraKickDeg + AmountDeg, 0.0f, CameraKickMaxDeg);
}

FRideAudioMix AKiteRiderPawn::ComputeAudioMix(float ApparentWindKnots, float BoardSpeedKnots, bool bOnWater, float LineTensionN)
{
	FRideAudioState State;
	State.ApparentWindKnots = ApparentWindKnots;
	State.BoardSpeedKnots = BoardSpeedKnots;
	State.bOnWater = bOnWater;
	State.LineTensionN = LineTensionN;
	State.bAirborne = !bOnWater;
	return ComputeAudioMix(State);
}

FRideAudioMix AKiteRiderPawn::ComputeAudioMix(const FRideAudioState& State)
{
	FRideAudioMix Mix;

	// Wind in the ears: nothing in a calm, loud and higher in a blow.
	const float WindAmount = FMath::Clamp(State.ApparentWindKnots / 40.0f, 0.0f, 1.0f);
	Mix.WindVolume = 0.72f * FMath::Pow(WindAmount, 1.3f);
	Mix.WindPitch = 0.8f + 0.5f * WindAmount;

	// Water under the board: only while it is on the water, a slosh when slow, a hiss at speed.
	const float SpeedAmount = FMath::Clamp(State.BoardSpeedKnots / 28.0f, 0.0f, 1.0f);
	Mix.WaterVolume = State.bOnWater ? 0.08f + 0.6f * FMath::Pow(SpeedAmount, 1.2f) : 0.0f;
	Mix.WaterPitch = 0.75f + 0.55f * SpeedAmount;

	// Lines singing under load: silent when slack, rising with the pull.
	const float LoadAmount = FMath::Clamp(State.LineTensionN / 3500.0f, 0.0f, 1.0f);
	Mix.LineVolume = 0.55f * FMath::Pow(LoadAmount, 1.2f);
	Mix.LinePitch = 0.7f + 0.9f * LoadAmount;

	// Spray: an edge driven hard at speed throws it; none in the air or standing still.
	Mix.SprayVolume = State.bOnWater ? 0.6f * FMath::Clamp(State.EdgeEffort, 0.0f, 1.0f) * FMath::Clamp(State.BoardSpeedKnots / 14.0f, 0.0f, 1.0f) : 0.0f;

	// The kite: hardly heard parked, a roar when it is flown fast through a turn or a loop.
	const float KiteSpeedAmount = FMath::Clamp((State.KiteAirspeedMS - 16.0f) / 24.0f, 0.0f, 1.0f);
	Mix.KiteVolume = 0.75f * FMath::Pow(KiteSpeedAmount, 1.2f);
	Mix.KitePitch = 0.8f + 0.6f * KiteSpeedAmount;

	// A canopy with no load in it flaps, as long as there is wind to flap it.
	Mix.FlutterVolume = 0.5f * FMath::Clamp(State.KiteLuff, 0.0f, 1.0f) * FMath::Clamp(State.ApparentWindKnots / 12.0f, 0.0f, 1.0f);

	// The music lifts while the rider is in the air.
	Mix.AirMusic = State.bAirborne ? 1.0f : 0.0f;
	return Mix;
}

void AKiteRiderPawn::UpdateAudioModulation(float DeltaTime)
{
	const FVector Vel = GetBoardVelocity();
	// The wind in the rider's ears: at chest height, where the kite says the rider feels it.
	const FVector EarLocation = GetActorLocation() + FVector(0.0f, 0.0f, Kite ? Kite->RiderWindHeightCm : 0.0f);
	const FVector TrueWind = Wind ? Wind->GetWindAt(EarLocation) : FVector::ZeroVector;

	FRideAudioState State;
	State.ApparentWindKnots = KiteUnits::CmSToKnots((TrueWind - Vel).Size());
	State.BoardSpeedKnots = KiteUnits::CmSToKnots(Vel.Size2D());
	State.bAirborne = BoardMovement && BoardMovement->GetBoardState() == EBoardState::Airborne;
	State.bOnWater = !State.bAirborne;
	if (BoardMovement)
	{
		State.EdgeEffort = FMath::Max(FMath::Abs(BoardMovement->GetEdgeInput()), BoardMovement->GetLoadAmount());
	}
	if (Kite)
	{
		State.LineTensionN = Kite->GetLineTensionN();
		if (!Kite->IsCrashed())
		{
			State.KiteAirspeedMS = Kite->GetAirspeedCmS() / KiteUnits::CmPerM;
			// Slack lines flap hardest; a stalled canopy and one with the bar right out less so.
			if (!Kite->AreLinesTaut())
			{
				State.KiteLuff = 1.0f;
			}
			else if (Kite->GetAngleOfAttackDeg() > Kite->StallAngleDeg)
			{
				State.KiteLuff = 0.7f;
			}
			else
			{
				State.KiteLuff = 0.5f * FMath::Clamp(1.0f - CurrentSheetInput / 0.15f, 0.0f, 1.0f);
			}
		}
	}
	const FRideAudioMix Target = ComputeAudioMix(State);

	// Eased so that a gust or the board leaving the water is heard as a swell, not a switch.
	const float Ease = 6.0f;
	AudioMix.WindVolume = FMath::FInterpTo(AudioMix.WindVolume, Target.WindVolume, DeltaTime, Ease);
	AudioMix.WindPitch = FMath::FInterpTo(AudioMix.WindPitch, Target.WindPitch, DeltaTime, Ease);
	AudioMix.WaterVolume = FMath::FInterpTo(AudioMix.WaterVolume, Target.WaterVolume, DeltaTime, 2.0f * Ease);
	AudioMix.WaterPitch = FMath::FInterpTo(AudioMix.WaterPitch, Target.WaterPitch, DeltaTime, Ease);
	AudioMix.LineVolume = FMath::FInterpTo(AudioMix.LineVolume, Target.LineVolume, DeltaTime, Ease);
	AudioMix.LinePitch = FMath::FInterpTo(AudioMix.LinePitch, Target.LinePitch, DeltaTime, Ease);
	AudioMix.SprayVolume = FMath::FInterpTo(AudioMix.SprayVolume, Target.SprayVolume, DeltaTime, 2.0f * Ease);
	AudioMix.KiteVolume = FMath::FInterpTo(AudioMix.KiteVolume, Target.KiteVolume, DeltaTime, Ease);
	AudioMix.KitePitch = FMath::FInterpTo(AudioMix.KitePitch, Target.KitePitch, DeltaTime, Ease);
	AudioMix.FlutterVolume = FMath::FInterpTo(AudioMix.FlutterVolume, Target.FlutterVolume, DeltaTime, Ease);
	// The air layer comes in quickly on take-off and lingers for a couple of seconds after landing.
	AudioMix.AirMusic = FMath::FInterpConstantTo(AudioMix.AirMusic, Target.AirMusic, DeltaTime, Target.AirMusic > AudioMix.AirMusic ? 2.5f : 0.45f);

	// The mix is what the ride calls for; the ambient volume setting scales what is played.
	auto Apply = [this](UAudioComponent* Loop, float Volume, float Pitch)
	{
		if (Loop)
		{
			Loop->SetVolumeMultiplier(Volume * AmbientVolume);
			Loop->SetPitchMultiplier(Pitch);
		}
	};
	Apply(WindLoopComponent, AudioMix.WindVolume, AudioMix.WindPitch);
	Apply(WaterLoopComponent, AudioMix.WaterVolume, AudioMix.WaterPitch);
	Apply(LineLoopComponent, AudioMix.LineVolume, AudioMix.LinePitch);
	Apply(SprayLoopComponent, AudioMix.SprayVolume, 1.0f);
	Apply(KiteLoopComponent, AudioMix.KiteVolume, AudioMix.KitePitch);
	Apply(FlutterLoopComponent, AudioMix.FlutterVolume, 1.0f);

	// Music sits under the sound of the ride. Its pitch never moves, or the layers would drift apart.
	if (MusicBaseComponent)
	{
		MusicBaseComponent->SetVolumeMultiplier(0.8f * MusicVolume);
	}
	if (MusicAirComponent)
	{
		MusicAirComponent->SetVolumeMultiplier(0.7f * MusicVolume * AudioMix.AirMusic);
	}
}

void AKiteRiderPawn::OnPauseTriggered(const FInputActionValue& Value)
{
	TogglePause();
}

void AKiteRiderPawn::TogglePause()
{
	// Escape reaches this twice on one press (the Enhanced Input action and the fallback key
	// binding), which opened the menu and closed it again in the same frame.
	if (LastPauseToggleFrame == GFrameCounter)
	{
		return;
	}
	LastPauseToggleFrame = GFrameCounter;

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (AKiteSurfHUD* HUD = Cast<AKiteSurfHUD>(PC->GetHUD()))
		{
			HUD->TogglePauseMenu();
		}
	}
}
