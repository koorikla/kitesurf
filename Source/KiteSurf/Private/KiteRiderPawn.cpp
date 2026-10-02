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
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "KiteSurf.h"
#include "KiteSurfHUD.h"
#include "UI/KiteSurfGameInstance.h"
#include "Engine/StaticMesh.h"
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

	// BoardMesh root
	BoardMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BoardMesh"));
	RootComponent = BoardMesh;

	// Setup default meshes and materials if available
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BoardMeshFinder(TEXT("/Game/Meshes/SM_KiteBoard"));
	if (BoardMeshFinder.Succeeded())
	{
		BoardMesh->SetStaticMesh(BoardMeshFinder.Object);
	}

	// RiderMesh attached to BoardMesh (standing on board)
	RiderMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("RiderMesh"));
	RiderMesh->SetupAttachment(RootComponent);
	RiderMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
	RiderMesh->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f)); // Face across the board in kitesurf stance

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> RiderMeshFinder(RiderCharacter::MannequinMeshPath);
	if (RiderMeshFinder.Succeeded())
	{
		RiderMesh->SetSkeletalMesh(RiderMeshFinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UAnimationAsset> RiderAnimFinder(RiderCharacter::MannequinIdlePath);
	if (RiderAnimFinder.Succeeded())
	{
		RiderMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		RiderMesh->SetAnimation(RiderAnimFinder.Object);
		RiderMesh->Play(true);
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
	RiderMesh->SetUsingAbsoluteRotation(true);
	ControlBarMesh->SetUsingAbsoluteLocation(true);
	ControlBarMesh->SetUsingAbsoluteRotation(true);

	// CameraBoom: pivots at chest height and is aimed in world space by UpdateCamera, so the
	// horizon stays level while the board pitches with the swell and rolls with the edge.
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->SetRelativeLocation(FVector(0.0f, 0.0f, 120.0f));
	CameraBoom->TargetArmLength = CameraArmLengthCm;
	CameraBoom->SetRelativeRotation(FRotator(CameraBoomPitchDeg, 0.0f, 0.0f));
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 3.0f;
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

void AKiteRiderPawn::ApplyScriptedInput(float Steer, float SheetRate, float Carve, float WeightShift, bool bLoop)
{
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
		SheetKite(CurrentSheetInput - DeltaY * MouseSheetSensitivity);
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
		Kite->StepKite(StepSeconds);
	}
	if (Kite && BoardMovement)
	{
		BoardMovement->AddExternalForce(Kite->GetLineForce());
	}
	if (BoardMovement)
	{
		BoardMovement->StepBoard(StepSeconds);
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
		UE_LOG(LogKiteSurf, Log, TEXT("kitecsv,t_s,rider_x,rider_y,rider_z,rider_vx,rider_vy,rider_vz,kite_x,kite_y,kite_z,kite_vx,kite_vy,kite_vz,tension_n,alpha_deg,cl,board_state,gust"));
		bLoggedTelemetryHeader = true;
	}
	// Positions in cm and velocities in cm/s, as the engine has them.
	const FVector RiderPos = SimLocation;
	const FVector RiderVel = BoardMovement->Velocity;
	const FVector KitePos = Kite->GetKiteWorldPosition();
	const FVector KiteVel = Kite->GetKiteVelocity();
	const FKiteStepDebug& KiteStep = Kite->GetLastStepDebug();
	const float Gust = Wind ? Wind->GetGustFactorAtTime(RiderPos, Kite->GetSimTimeSeconds()) : 1.0f;
	UE_LOG(LogKiteSurf, Log, TEXT("kitecsv,%.4f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.2f,%.3f,%d,%.3f"),
		SimTimeSeconds, RiderPos.X, RiderPos.Y, RiderPos.Z, RiderVel.X, RiderVel.Y, RiderVel.Z,
		KitePos.X, KitePos.Y, KitePos.Z, KiteVel.X, KiteVel.Y, KiteVel.Z,
		Kite->GetLineTensionN(), KiteStep.AlphaDeg, KiteStep.LiftCoefficient, static_cast<int32>(BoardMovement->GetBoardState()), Gust);
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
	const FString KiteText = FString::Printf(TEXT("airspeed %.1f m/s  alpha %.1f deg\nCl %.2f  Cd %.2f\ntension %.0f N %s\nclock %.0f  depth %.0f deg\nsteps last frame %d"),
		Kite->GetAirspeedCmS() / KiteUnits::CmPerM, KiteStep.AlphaDeg, KiteStep.LiftCoefficient, KiteStep.DragCoefficient,
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
	const FBoardStepDebug& BoardStep = BoardMovement->GetLastStepDebug();
	const FVector BoardAt = RiderAt + FVector(0.0f, 0.0f, DebugBoardForceHeightCm);
	DrawDebugVector(World, BoardAt, BoardStep.GripForceN, DebugCmPerN, FColor::Orange);
	DrawDebugVector(World, BoardAt, BoardStep.DriveForceN, DebugCmPerN, FColor::Green);
	DrawDebugVector(World, BoardAt, BoardStep.DragForceN, DebugCmPerN, FColor::Red);
	DrawDebugVector(World, Chest, BoardStep.AirDragN, DebugCmPerN, FColor::Silver); // in the air only

	float HeadingDeg = FRotator::NormalizeAxis(GetActorRotation().Yaw);
	HeadingDeg = HeadingDeg < 0.0f ? HeadingDeg + 360.0f : HeadingDeg;
	const float Gust = Wind ? Wind->GetGustFactorAtTime(RiderAt, SimTime) : 1.0f;
	const FString RiderText = FString::Printf(TEXT("%.1f kn  heading %.0f\nleeway %.1f deg  edge %.1f deg\n%s%s\ngust %.2f"),
		KiteUnits::CmSToKnots(BoardVelocityNow.Size2D()), HeadingDeg, BoardStep.LeewayDeg, GetActorRotation().Roll,
		BoardStateName(BoardMovement->GetBoardState()), BoardMovement->IsFloating() ? TEXT(", floating") : TEXT(""), Gust);
	DrawDebugString(World, RiderAt + FVector(0.0f, 0.0f, DebugRiderTextHeightCm), RiderText, nullptr, FColor::White, 0.0f, true);

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

	// With the motion bar in the rider's hands, the stick, triggers and keys leave the bar alone.
	if (!bMotionBarActive && !FMath::IsNearlyZero(SheetRateInput))
	{
		SheetKite(CurrentSheetInput + SheetRateInput * SheetRatePerSec * DeltaTime);
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

	// Riders without a posed static mesh are the animated mannequin.
	const bool bRobot = RiderCharacter::GetStaticMeshPath(RiderCharacter) == nullptr;
	if (RiderMesh)
	{
		RiderMesh->SetVisibility(bRobot);
	}
	// The jointed riders share a rig; each has its own torso and limb parts.
	const TCHAR* RiderName = RiderCharacter == ERiderCharacter::Wetsuit ? TEXT("Wetsuit") : TEXT("Santa");
	auto SetPart = [RiderName, bRobot](UStaticMeshComponent* Component, const TCHAR* PartName)
	{
		if (!Component)
		{
			return;
		}
		if (!bRobot)
		{
			const FString MeshPath = FString::Printf(TEXT("/Game/Meshes/SM_Rider%s_%s"), RiderName, PartName);
			if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath))
			{
				Component->SetStaticMesh(Mesh);
			}
		}
		Component->SetVisibility(!bRobot);
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
		RiderStanceSide = ChooseStanceSide(BoardYawDeg, RiderFacingYawDeg);

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

	// Lean away from the pull of the lines, whichever way the rider is facing. On the water that
	// is leaning out against the kite; in the air the rider hangs from the harness at their
	// waist, so the lower the kite the further back the shoulders go. Floating, they lie back
	// in the water with the board out in front.
	FVector BodyUp = FVector::UpVector;
	if (bHasKite)
	{
		const float FullLeanForce = 60000.0f; // 600 N
		const float Load = FMath::Clamp(Kite->GetLineForce().Size() / FullLeanForce, 0.0f, 1.0f);
		float PullLeanDeg = RiderMaxLeanDeg * FMath::Clamp(Kite->GetLineForce().Size2D() / FullLeanForce, 0.0f, 1.0f);
		if (bAirborne)
		{
			const float LineOffVerticalDeg = 90.0f - Kite->GetElevationDeg();
			PullLeanDeg = FMath::Min(RiderAirHangLeanDeg, 0.6f * LineOffVerticalDeg) * Load;
		}
		BodyUp -= TowardsKite * FMath::Tan(FMath::DegreesToRadians(PullLeanDeg));
	}
	if (BoardMovement && BoardMovement->FloatSubmersionCm > 0.0f)
	{
		const float FloatLeanDeg = RiderFloatLeanDeg * FMath::Clamp(BoardMovement->GetFloatDepthCm() / BoardMovement->FloatSubmersionCm, 0.0f, 1.0f);
		BodyUp -= Facing * FMath::Tan(FMath::DegreesToRadians(FloatLeanDeg));
	}
	// Loading: the rider sits back away from the kite, weight low over the back of the board.
	const float Load = BoardMovement ? BoardMovement->GetLoadAmount() : 0.0f;
	if (Load > 0.0f)
	{
		const FVector Away = bHasKite ? -TowardsKite : -Facing;
		BodyUp += Away * FMath::Tan(FMath::DegreesToRadians(RiderLoadLeanDeg * Load));
	}
	const FQuat BodyQuat = FRotationMatrix::MakeFromXZ(Facing, BodyUp.GetSafeNormal()).ToQuat();

	// The jointed rider: feet in the straps wherever the board goes, pelvis over them along the
	// body's lean and lower in a crouch, knees bending to fit.
	FRiderRigInput RigInput;
	RigInput.Board = GetActorTransform();
	RigInput.Facing = Facing;
	RigInput.BodyUp = BodyUp.GetSafeNormal();
	RigInput.Crouch = Load;
	RiderPose = RiderRig::SolveBody(RigInput);

	if (RiderMesh)
	{
		// The mannequin's front is +Y in mesh space.
		RiderMesh->SetWorldRotation(BodyQuat * FQuat(FRotator(0.0f, -90.0f, 0.0f)));
	}

	// The lines pull on the harness hook at the front of the rider's waist. The bar rides on them
	// just beyond the hook, further out the more it is sheeted out, and always in front of the
	// body: when the kite is behind a spinning rider the lines come over their shoulder.
	HarnessHookPosition = RiderPose.Pelvis + RiderPose.Torso.RotateVector(HarnessHookOffsetCm);
	if (Kite)
	{
		FVector LineDir = bHasKite ? (Kite->GetKiteWorldPosition() - HarnessHookPosition).GetSafeNormal() : Facing;
		const float MinForward = 0.15f;
		const float Forward = FVector::DotProduct(LineDir, Facing);
		if (Forward < MinForward)
		{
			LineDir = (LineDir + Facing * (MinForward - Forward)).GetSafeNormal();
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
		const FVector RiderRight = FVector::CrossProduct(FVector::UpVector, Facing);
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

void AKiteRiderPawn::UpdateCamera(float DeltaTime)
{
	if (!CameraBoom || !FollowCamera)
	{
		return;
	}

	const float HeadingYawDeg = GetActorRotation().Yaw;
	float TargetYawDeg = HeadingYawDeg;
	float TargetLookPitchDeg = CameraMinLookPitchDeg;

	if (HasKitePosition())
	{
		// Look along the heading, but never further from the kite than the offset that keeps it in frame.
		const FVector SmoothedKitePosition = GetActorLocation() + SmoothedKiteOffset;
		const FVector PivotToKite = SmoothedKitePosition - CameraBoom->GetComponentLocation();
		const float KiteYawDeg = PivotToKite.Rotation().Yaw;
		const float HeadingFromKiteDeg = FMath::FindDeltaAngleDegrees(KiteYawDeg, HeadingYawDeg);
		TargetYawDeg = KiteYawDeg + FMath::Clamp(HeadingFromKiteDeg, -CameraMaxKiteYawOffsetDeg, CameraMaxKiteYawOffsetDeg);

		// Tilt up only as far as needed to keep a high kite below the top of the screen.
		const FVector CameraToKite = SmoothedKitePosition - FollowCamera->GetComponentLocation();
		const float KiteElevationFromCameraDeg = FMath::RadiansToDegrees(FMath::Atan2(CameraToKite.Z, CameraToKite.Size2D()));
		TargetLookPitchDeg = FMath::Clamp(KiteElevationFromCameraDeg - CameraKiteHeadroomDeg, CameraMinLookPitchDeg, CameraMaxLookPitchDeg);
	}

	if (bViewInitialized)
	{
		const float YawStepDeg = FMath::FindDeltaAngleDegrees(CameraYawDeg, TargetYawDeg) * FMath::Clamp(CameraTurnSpeed * DeltaTime, 0.0f, 1.0f);
		CameraYawDeg = FRotator::NormalizeAxis(CameraYawDeg + YawStepDeg);
		CameraLookPitchDeg = FMath::FInterpTo(CameraLookPitchDeg, TargetLookPitchDeg, DeltaTime, CameraTurnSpeed);
	}
	else
	{
		CameraYawDeg = TargetYawDeg;
		CameraLookPitchDeg = TargetLookPitchDeg;
	}

	// The boom keeps the camera above the water; the camera itself tilts to look up at the kite.
	CameraBoom->TargetArmLength = CameraArmLengthCm;
	CameraBoom->SetWorldRotation(FRotator(CameraBoomPitchDeg, CameraYawDeg, 0.0f));
	FollowCamera->SetRelativeRotation(FRotator(CameraLookPitchDeg - CameraBoomPitchDeg, 0.0f, 0.0f));
	FollowCamera->SetFieldOfView(CameraFOVDeg);
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
