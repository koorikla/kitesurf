#include "KiteRiderPawn.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "WindComponent.h"
#include "BoardMovementComponent.h"
#include "BoardWakeComponent.h"
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

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> RiderMeshFinder(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"));
	if (RiderMeshFinder.Succeeded())
	{
		RiderMesh->SetSkeletalMesh(RiderMeshFinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UAnimationAsset> RiderAnimFinder(TEXT("/Game/Characters/Mannequins/Anims/MM_Idle"));
	if (RiderAnimFinder.Succeeded())
	{
		RiderMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		RiderMesh->SetAnimation(RiderAnimFinder.Object);
		RiderMesh->Play(true);
	}

	// Posed riders stand on the board; which one is shown is set by SetRiderCharacter.
	RiderStaticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RiderStaticMesh"));
	RiderStaticMesh->SetupAttachment(RootComponent);
	RiderStaticMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 2.0f));
	RiderStaticMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RiderStaticMesh->SetUsingAbsoluteRotation(true);

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
	RiderSwitchDelaySeconds = 0.4f;
	RiderSwitchTurnRateDeg = 540.0f;
	HarnessHookOffsetCm = FVector(16.0f, 0.0f, 100.0f);

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

	// Foam trail and spray behind the board
	Wake = CreateDefaultSubobject<UBoardWakeComponent>(TEXT("Wake"));

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

	auto FindSound = [](const TCHAR* SoundPath) -> USoundBase*
	{
		ConstructorHelpers::FObjectFinder<USoundBase> Finder(SoundPath);
		return Finder.Succeeded() ? Finder.Object : nullptr;
	};
	PopSound = FindSound(TEXT("/Game/Audio/SW_Pop"));
	LandingSound = FindSound(TEXT("/Game/Audio/SW_Landing"));
	CrashSound = FindSound(TEXT("/Game/Audio/SW_Crash"));
	ResetSound = FindSound(TEXT("/Game/Audio/SW_ResetCue"));

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
}

void AKiteRiderPawn::BeginPlay()
{
	Super::BeginPlay();

	if (const UWorld* World = GetWorld())
	{
		if (const UKiteSurfGameInstance* GI = Cast<UKiteSurfGameInstance>(World->GetGameInstance()))
		{
			SetRiderCharacter(GI->RiderCharacter);
		}
	}

	if (BoardMovement)
	{
		BoardMovement->OnBoardCrash.AddDynamic(this, &AKiteRiderPawn::HandleBoardCrash);
		BoardMovement->OnBoardReset.AddDynamic(this, &AKiteRiderPawn::HandleBoardReset);
		BoardMovement->OnBoardLanding.AddDynamic(this, &AKiteRiderPawn::HandleBoardLanding);
	}

	for (UAudioComponent* Loop : { WindLoopComponent.Get(), WaterLoopComponent.Get(), LineLoopComponent.Get() })
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
			EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Triggered, this, &AKiteRiderPawn::OnJumpTriggered);
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

void AKiteRiderPawn::OnSteerTriggered(const FInputActionValue& Value)
{
	KeySteerInput = FMath::Clamp(Value.Get<float>(), -1.0f, 1.0f);
	SteerKite(KeySteerInput + MouseSteerInput);
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

void AKiteRiderPawn::OnJumpTriggered(const FInputActionValue& Value)
{
	Jump();
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
		if (PopSound)
		{
			UGameplayStatics::PlaySound2D(this, PopSound, 0.8f);
		}
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

void AKiteRiderPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// UKiteComponent computes the aerodynamic line force (kg*cm/s^2);
	// UBoardMovementComponent owns all velocity integration and hydrodynamics.
	if (Kite && BoardMovement)
	{
		BoardMovement->AddExternalForce(Kite->GetLineForce());
	}

	if (!FMath::IsNearlyZero(SheetRateInput))
	{
		SheetKite(CurrentSheetInput + SheetRateInput * SheetRatePerSec * DeltaTime);
	}

	if (GetController())
	{
		UpdateMouseBar();
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
	UpdateCamera(DeltaTime);
	bViewInitialized = true;

	const FVector Vel = GetBoardVelocity();
	ensureAlwaysMsgf(!Vel.ContainsNaN(), TEXT("AKiteRiderPawn::Tick: BoardVelocity contains NaN or Inf: %s"), *Vel.ToString());

	UpdateAudioModulation(DeltaTime);
}

void AKiteRiderPawn::SetRiderCharacter(ERiderCharacter InCharacter)
{
	RiderCharacter = RiderCharacter::FromIndex(static_cast<int32>(InCharacter));

	const bool bRobot = RiderCharacter == ERiderCharacter::Robot;
	if (RiderMesh)
	{
		RiderMesh->SetVisibility(bRobot);
	}
	if (RiderStaticMesh)
	{
		if (!bRobot)
		{
			const TCHAR* MeshPath = RiderCharacter == ERiderCharacter::Wetsuit ? TEXT("/Game/Meshes/SM_RiderWetsuit") : TEXT("/Game/Meshes/SM_RiderSanta");
			if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath))
			{
				RiderStaticMesh->SetStaticMesh(Mesh);
			}
		}
		RiderStaticMesh->SetVisibility(!bRobot);
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
	const FQuat BodyQuat = FRotationMatrix::MakeFromXZ(Facing, BodyUp.GetSafeNormal()).ToQuat();

	if (RiderMesh)
	{
		// The mannequin's front is +Y in mesh space.
		RiderMesh->SetWorldRotation(BodyQuat * FQuat(FRotator(0.0f, -90.0f, 0.0f)));
	}
	if (RiderStaticMesh)
	{
		// The posed riders are built facing +X.
		RiderStaticMesh->SetWorldRotation(BodyQuat);
	}

	// The lines pull on the harness hook at the front of the rider's waist. The bar rides on them
	// just beyond the hook, further out the more it is sheeted out, and always in front of the
	// body: when the kite is behind a spinning rider the lines come over their shoulder.
	HarnessHookPosition = GetActorLocation() + BodyQuat.RotateVector(HarnessHookOffsetCm);
	if (Kite)
	{
		FVector LineDir = bHasKite ? (Kite->GetKiteWorldPosition() - HarnessHookPosition).GetSafeNormal() : Facing;
		const float MinForward = 0.15f;
		const float Forward = FVector::DotProduct(LineDir, Facing);
		if (Forward < MinForward)
		{
			LineDir = (LineDir + Facing * (MinForward - Forward)).GetSafeNormal();
		}
		const float BarReachCm = 42.0f + 25.0f * (1.0f - CurrentSheetInput);
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
	if (BoardMovement)
	{
		BoardMovement->ResetToTack(8.0f);
	}
}

void AKiteRiderPawn::HandleBoardCrash(float Intensity)
{
	if (CrashSound)
	{
		UGameplayStatics::PlaySound2D(this, CrashSound, FMath::Clamp(Intensity, 0.4f, 1.2f));
	}
}

void AKiteRiderPawn::HandleBoardReset()
{
	if (ResetSound)
	{
		UGameplayStatics::PlaySound2D(this, ResetSound, 0.5f);
	}
}

void AKiteRiderPawn::HandleBoardLanding(float LandingG)
{
	if (LandingSound)
	{
		// A harder landing is a louder, deeper splash.
		const float Hardness = FMath::Clamp((LandingG - 1.0f) / 5.0f, 0.0f, 1.0f);
		UGameplayStatics::PlaySound2D(this, LandingSound, 0.45f + 0.65f * Hardness, 1.1f - 0.3f * Hardness);
	}
}

FRideAudioMix AKiteRiderPawn::ComputeAudioMix(float ApparentWindKnots, float BoardSpeedKnots, bool bOnWater, float LineTensionN)
{
	FRideAudioMix Mix;

	// Wind in the ears: nothing in a calm, loud and higher in a blow.
	const float WindAmount = FMath::Clamp(ApparentWindKnots / 40.0f, 0.0f, 1.0f);
	Mix.WindVolume = 0.9f * FMath::Pow(WindAmount, 1.3f);
	Mix.WindPitch = 0.8f + 0.5f * WindAmount;

	// Water under the board: only while it is on the water, a slosh when slow, a hiss at speed.
	const float SpeedAmount = FMath::Clamp(BoardSpeedKnots / 28.0f, 0.0f, 1.0f);
	Mix.WaterVolume = bOnWater ? 0.08f + 0.75f * FMath::Pow(SpeedAmount, 1.2f) : 0.0f;
	Mix.WaterPitch = 0.75f + 0.55f * SpeedAmount;

	// Lines singing under load: silent when slack, rising with the pull.
	const float LoadAmount = FMath::Clamp(LineTensionN / 3500.0f, 0.0f, 1.0f);
	Mix.LineVolume = 0.55f * FMath::Pow(LoadAmount, 1.2f);
	Mix.LinePitch = 0.7f + 0.9f * LoadAmount;
	return Mix;
}

void AKiteRiderPawn::UpdateAudioModulation(float DeltaTime)
{
	const FVector Vel = GetBoardVelocity();
	const FVector TrueWind = Wind ? Wind->GetWindAt(GetActorLocation()) : FVector::ZeroVector;
	const float ApparentWindKnots = KiteUnits::CmSToKnots((TrueWind - Vel).Size());
	const bool bOnWater = BoardMovement && BoardMovement->GetBoardState() != EBoardState::Airborne;
	const float LineTensionN = Kite ? Kite->GetLineTensionN() : 0.0f;

	const FRideAudioMix Target = ComputeAudioMix(ApparentWindKnots, KiteUnits::CmSToKnots(Vel.Size2D()), bOnWater, LineTensionN);

	// Eased so that a gust or the board leaving the water is heard as a swell, not a switch.
	const float Ease = 6.0f;
	AudioMix.WindVolume = FMath::FInterpTo(AudioMix.WindVolume, Target.WindVolume, DeltaTime, Ease);
	AudioMix.WindPitch = FMath::FInterpTo(AudioMix.WindPitch, Target.WindPitch, DeltaTime, Ease);
	AudioMix.WaterVolume = FMath::FInterpTo(AudioMix.WaterVolume, Target.WaterVolume, DeltaTime, 2.0f * Ease);
	AudioMix.WaterPitch = FMath::FInterpTo(AudioMix.WaterPitch, Target.WaterPitch, DeltaTime, Ease);
	AudioMix.LineVolume = FMath::FInterpTo(AudioMix.LineVolume, Target.LineVolume, DeltaTime, Ease);
	AudioMix.LinePitch = FMath::FInterpTo(AudioMix.LinePitch, Target.LinePitch, DeltaTime, Ease);

	auto Apply = [](UAudioComponent* Loop, float Volume, float Pitch)
	{
		if (Loop)
		{
			Loop->SetVolumeMultiplier(Volume);
			Loop->SetPitchMultiplier(Pitch);
		}
	};
	Apply(WindLoopComponent, AudioMix.WindVolume, AudioMix.WindPitch);
	Apply(WaterLoopComponent, AudioMix.WaterVolume, AudioMix.WaterPitch);
	Apply(LineLoopComponent, AudioMix.LineVolume, AudioMix.LinePitch);
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
