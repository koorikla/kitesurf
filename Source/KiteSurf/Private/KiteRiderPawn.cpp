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
	RiderSwitchDelaySeconds = 0.4f;
	RiderSwitchTurnRateDeg = 540.0f;
	HarnessHookOffsetCm = FVector(22.0f, 0.0f, 16.0f);

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

	// The rider's rotation in the air: stepped by StepSimulation before the board (it does not tick).
	RiderAttitude = CreateDefaultSubobject<URiderAttitudeComponent>(TEXT("RiderAttitude"));
	bUseRiderAttitude = true;
	PreWindBuildSeconds = 0.5f;
	PreWindStickThreshold = 0.3f;
	AirRotationDeadzone = 0.15f;
	VerticalAccelFilterSeconds = 0.1f;
	RiderHandoverSeconds = 0.2f;

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
	KiteCrashSound = FindSound(TEXT("/Game/Audio/SW_KiteCrash"));
	RelaunchSound = FindSound(TEXT("/Game/Audio/SW_Relaunch"));
	AgroundSound = FindSound(TEXT("/Game/Audio/SW_Aground"));
	SharkSound = FindSound(TEXT("/Game/Audio/SW_Shark"));

	RiderCharacter = ERiderCharacter::Santa;
	SetRiderCharacter(RiderCharacter);

	CurrentSteerInput = 0.0f;
	CurrentSheetInput = 0.0f;
	SheetRateInput = 0.0f;
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
		SteerKite(KeySteerInput + MouseSteerInput);
	}
}

void AKiteRiderPawn::RecentreMotionBar()
{
	bMotionRecentrePending = true;
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
		// However the pad is being held now is "bar level": no jump in steering or power.
		MotionBarMapping.Calibrate(MotionFilter.GetRollDeg(), MotionFilter.GetPitchDeg(), CurrentSheetInput);
		bMotionRecentrePending = false;
	}
	bMotionBarActive = true;

	// The right stick is taken out of the steering: the keys and the mouse still add to the tilt.
	float StickSteer = 0.0f;
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		StickSteer = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX);
	}
	SteerKite(KeySteerInput - StickSteer + MouseSteerInput + MotionBarMapping.GetSteer(MotionFilter.GetRollDeg()));
	SheetKite(MotionBarMapping.GetSheet(MotionFilter.GetPitchDeg()));
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
	PreWindStick = Stick.GetClampedToMaxSize(1.0f);
}

void AKiteRiderPawn::SetAirRotationInput(FVector2D Stick)
{
	AirRotationStick = Stick.GetClampedToMaxSize(1.0f);
}

void AKiteRiderPawn::SetTuck(float Amount)
{
	TuckInput = FMath::Clamp(Amount, 0.0f, 1.0f);
}

void AKiteRiderPawn::OnWeightShiftTriggered(const FInputActionValue& Value)
{
	if (BoardMovement)
	{
		BoardMovement->SetWeightShift(Value.Get<float>());
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
		if (bMotionBarActive)
		{
			MotionBarMapping.SheetAtNeutral = FMath::Clamp(MotionBarMapping.SheetAtNeutral - DeltaY * MouseSheetSensitivity, 0.0f, 1.0f);
			SheetKite(MotionBarMapping.GetSheet(MotionFilter.GetPitchDeg()));
		}
		else
		{
			SheetKite(CurrentSheetInput - DeltaY * MouseSheetSensitivity);
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
	SetSheetRateInput(Value.Get<float>());
}

void AKiteRiderPawn::SetSheetRateInput(float Axis)
{
	// The bar stays where it is put: the input moves it in or out (see Tick) instead of setting it.
	SheetRateInput = FMath::Clamp(Axis, -1.0f, 1.0f);
}

void AKiteRiderPawn::OnEdgeTriggered(const FInputActionValue& Value)
{
	EdgeBoard(Value.Get<float>());
}

void AKiteRiderPawn::OnJumpPressed(const FInputActionValue& Value)
{
	SetLoadHeld(true);
}

void AKiteRiderPawn::OnJumpReleased(const FInputActionValue& Value)
{
	ReleaseLoadAndPop();
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
	CurrentSheetInput = FMath::Clamp(Amount, 0.0f, 1.0f);
	if (Kite)
	{
		Kite->SheetKite(CurrentSheetInput);
	}
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
	if (TrickTracker) TrickTracker->StepTracker(StepSeconds);

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

FVector AKiteRiderPawn::ComputeLevelBodyUp(const FVector& Facing, const FVector& TowardsKite, bool bHasKite, bool bAirborne, float Load) const
{
	// Lean away from the pull of the lines, whichever way the rider is facing. On the water that
	// is leaning out against the kite; in the air the rider hangs from the harness at their
	// waist, so the lower the kite the further back the shoulders go. Floating, they lie back
	// in the water with the board out in front.
	FVector BodyUp = FVector::UpVector;
	if (bHasKite && Kite)
	{
		const float FullLeanForce = 60000.0f; // 600 N
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
	// Loading: the rider sits back away from the kite, weight low over the back of the board.
	if (Load > 0.0f)
	{
		const FVector Away = bHasKite ? -TowardsKite : -Facing;
		BodyUp += Away * FMath::Tan(FMath::DegreesToRadians(RiderLoadLeanDeg * Load));
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
	const FVector BodyUp = ComputeLevelBodyUp(Facing, TowardsKite, bHasKite, false, BoardMovement ? BoardMovement->GetLoadAmount() : 0.0f).GetSafeNormal();
	return FRotationMatrix::MakeFromZX(BodyUp.IsNearlyZero() ? FVector::UpVector : BodyUp, Facing).ToQuat();
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
		}
		else
		{
			PreWindAmount = 0.0f;
			PreWindDirection = FVector2D::ZeroVector;
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
	In.Tuck = TuckInput;
	In.PreWindStick = PreWindDirection;
	In.PreWindAmount = PreWindAmount;
	In.TakeoffLoad = LastGroundLoad;
	In.TakeoffEdgeHold = LastGroundEdgeHold;
	if (bTakeoff)
	{
		TravelSideSigma = RiderAxes::TravelSide(In.SlavedBodyQuat, Velocity, RootQuat.GetAxisX());
	}
	In.TravelSideSigma = TravelSideSigma;

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
		UE_LOG(LogKiteSurf, Log, TEXT("kitecsv,t_s,rider_x,rider_y,rider_z,rider_vx,rider_vy,rider_vz,kite_x,kite_y,kite_z,kite_vx,kite_vy,kite_vz,tension_n,alpha_deg,cl,board_state,gust,heel_deg,leeway_deg,side_n,normal_side_n,board_drag_n,water_z,surface_vz,water_up_n,absorbing"));
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
	// on the board (N) and whether the touchdown absorber is on.
	UE_LOG(LogKiteSurf, Log, TEXT("kitecsv,%.4f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.2f,%.3f,%d,%.3f,%.2f,%.2f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%d"),
		SimTimeSeconds, RiderPos.X, RiderPos.Y, RiderPos.Z, RiderVel.X, RiderVel.Y, RiderVel.Z,
		KitePos.X, KitePos.Y, KitePos.Z, KiteVel.X, KiteVel.Y, KiteVel.Z,
		Kite->GetLineTensionN(), KiteStep.AlphaDeg, KiteStep.LiftCoefficient, static_cast<int32>(BoardMovement->GetBoardState()), Gust,
		BoardStep.HeelDeg, BoardStep.LeewayDeg, FVector::DotProduct(BoardStep.SideForceN, BoardRight), FVector::DotProduct(BoardStep.NormalSideForceN, BoardRight), BoardStep.DragForceN.Size(),
		BoardStep.WaterHeightCm, BoardStep.SurfaceVerticalSpeedCmS, BoardStep.WaterVerticalForceN, BoardStep.bAbsorbing ? 1 : 0);
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
	const FString RiderText = FString::Printf(TEXT("%.1f kn  heading %.0f\nleeway %.1f deg  heel %.1f deg\nside %.0f N  normal %.0f N\n%s%s\nwater %.0f cm rising %.1f m/s, holds %.0f N%s\nlast landing %.1f g%s\ngust %.2f"),
		KiteUnits::CmSToKnots(BoardVelocityNow.Size2D()), HeadingDeg, BoardStep.LeewayDeg, BoardStep.HeelDeg, BoardStep.SideForceN.Size(), BoardStep.NormalSideForceN.Size(),
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

	// The stick, triggers and keys still adjust the bar when the motion controller is in use.
	if (!FMath::IsNearlyZero(SheetRateInput))
	{
		if (bMotionBarActive)
		{
			MotionBarMapping.SheetAtNeutral = FMath::Clamp(MotionBarMapping.SheetAtNeutral + SheetRateInput * SheetRatePerSec * DeltaTime, 0.0f, 1.0f);
			SheetKite(MotionBarMapping.GetSheet(MotionFilter.GetPitchDeg()));
		}
		else
		{
			SheetKite(CurrentSheetInput + SheetRateInput * SheetRatePerSec * DeltaTime);
		}
	}

	if (GetController())
	{
		UpdateMouseBar();
	}

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
		BackToKiteSeconds = bBackToKite ? BackToKiteSeconds + DeltaTime : 0.0f;
		if (BackToKiteSeconds > RiderSwitchDelaySeconds)
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
	const FVector BodyUp = ComputeLevelBodyUp(Facing, TowardsKite, bHasKite, bAirborne && !bAttitudeLive, Load);
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
	// The bar hangs in front of the body the rig drew: the level facing on the water, the body's own in the air.
	const bool bBodyPose = RigInput.BodyQuat.IsSet();
	const FVector BarFacing = bBodyPose ? RiderPose.Torso.GetAxisX() : Facing;

	// The lines pull on the harness hook at the front of the rider's waist. The bar rides on them
	// just beyond the hook, further out the more it is sheeted out, and always in front of the
	// body: when the kite is behind a spinning rider the lines come over their shoulder.
	HarnessHookPosition = RiderPose.Pelvis + RiderPose.Torso.RotateVector(HarnessHookOffsetCm);
	if (Kite)
	{
		FVector LineDir = bHasKite ? (Kite->GetKiteWorldPosition() - HarnessHookPosition).GetSafeNormal() : BarFacing;
		const float MinForward = 0.15f;
		const float Forward = FVector::DotProduct(LineDir, BarFacing);
		if (Forward < MinForward)
		{
			LineDir = (LineDir + BarFacing * (MinForward - Forward)).GetSafeNormal();
		}
		// The bar is never further up the lines than the rider's arms reach: leaning back with the
		// kite low, it comes in closer to the hook.
		const float HandSpacingCm = 14.0f;
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
		const FVector BarCentre = HarnessHookPosition + LineDir * BarReachCm;

		// The bar is held square to the lines across the rider's body, and tilts with the steering.
		const FVector RiderRight = bBodyPose ? RiderPose.Torso.GetAxisY() : FVector::CrossProduct(FVector::UpVector, Facing);
		FVector Span = (RiderRight - FVector::DotProduct(RiderRight, LineDir) * LineDir).GetSafeNormal();
		if (Span.IsNearlyZero())
		{
			Span = RiderRight;
		}
		const float TiltRad = FMath::DegreesToRadians(CurrentSteerInput * 25.0f);
		const FVector TiltedSpan = Span * FMath::Cos(TiltRad) + FVector::CrossProduct(LineDir, Span) * FMath::Sin(TiltRad);
		const float BarHalfWidthCm = 25.0f;
		Kite->SetBarEnds(BarCentre - TiltedSpan * BarHalfWidthCm, BarCentre + TiltedSpan * BarHalfWidthCm);
		if (ControlBarMesh)
		{
			ControlBarMesh->SetWorldLocationAndRotation(BarCentre, FRotationMatrix::MakeFromXY(LineDir, TiltedSpan).ToQuat());
		}

		// Hands on the bar, either side of its middle; the elbows bend to reach.
		RiderRig::SolveArms(RiderPose, BarCentre - TiltedSpan * HandSpacingCm, BarCentre + TiltedSpan * HandSpacingCm);
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

	// The boom keeps the camera above the water; the camera itself tilts to look up at the kite.
	// Neither ever rolls, so the horizon stays level.
	CameraBoom->TargetArmLength = CameraCurrentArmCm;
	CameraBoom->SetWorldRotation(FRotator(CameraCurrentBoomPitchDeg, CameraYawDeg, 0.0f));
	FollowCamera->SetRelativeRotation(FRotator(CameraLookPitchDeg - CameraCurrentBoomPitchDeg, 0.0f, 0.0f));
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
