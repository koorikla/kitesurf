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
#include "GameFramework/PlayerController.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

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

	// ControlBarMesh attached to BoardMesh, positioned in front of rider chest height
	ControlBarMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ControlBarMesh"));
	ControlBarMesh->SetupAttachment(RootComponent);
	ControlBarMesh->SetRelativeLocation(FVector(40.0f, 0.0f, 100.0f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> BarMeshFinder(TEXT("/Game/Meshes/SM_ControlBar"));
	if (BarMeshFinder.Succeeded())
	{
		ControlBarMesh->SetStaticMesh(BarMeshFinder.Object);
	}

	SheetRatePerSec = 0.8f;
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

	// The rider faces the kite and leans against it rather than turning with the board (see UpdateRiderPose).
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

	// Procedural audio components
	AudioBedComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("AudioBedComponent"));
	AudioBedComponent->SetupAttachment(RootComponent);
	AudioBedComponent->bAutoActivate = false;

	static ConstructorHelpers::FObjectFinder<USoundBase> BedFinder(TEXT("/Game/Audio/MS_AudioBed"));
	if (BedFinder.Succeeded())
	{
		AudioBedComponent->SetSound(BedFinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<USoundBase> PopFinder(TEXT("/Game/Audio/MS_Pop"));
	if (PopFinder.Succeeded())
	{
		PopSound = PopFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<USoundBase> LandingFinder(TEXT("/Game/Audio/MS_Landing"));
	if (LandingFinder.Succeeded())
	{
		LandingSound = LandingFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<USoundBase> CrashFinder(TEXT("/Game/Audio/MS_Crash"));
	if (CrashFinder.Succeeded())
	{
		CrashSound = CrashFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<USoundBase> ResetFinder(TEXT("/Game/Audio/MS_ResetCue"));
	if (ResetFinder.Succeeded())
	{
		ResetSound = ResetFinder.Object;
	}

	CurrentSteerInput = 0.0f;
	CurrentSheetInput = 0.0f;
	SheetRateInput = 0.0f;
	KeySteerInput = 0.0f;
	MouseSteerInput = 0.0f;
	bLoopKeyHeld = false;
	SmoothedKiteOffset = FVector::ZeroVector;
	CameraYawDeg = 0.0f;
	CameraLookPitchDeg = 0.0f;
	RiderFacingYawDeg = 0.0f;
	bViewInitialized = false;
	LastPauseToggleFrame = MAX_uint64;
	KiteAzimuthDeg = 0.0f;
	BoardVelocity = FVector::ZeroVector;
}

void AKiteRiderPawn::BeginPlay()
{
	Super::BeginPlay();

	if (BoardMovement)
	{
		BoardMovement->OnBoardCrash.AddDynamic(this, &AKiteRiderPawn::HandleBoardCrash);
		BoardMovement->OnBoardReset.AddDynamic(this, &AKiteRiderPawn::HandleBoardReset);
		BoardMovement->OnBoardLanding.AddDynamic(this, &AKiteRiderPawn::HandleBoardLanding);
	}

	if (AudioBedComponent && AudioBedComponent->GetSound())
	{
		AudioBedComponent->Play();
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
		if (EdgePressureAction)
		{
			EnhancedInputComponent->BindAction(EdgePressureAction, ETriggerEvent::Triggered, this, &AKiteRiderPawn::OnEdgePressureTriggered);
			EnhancedInputComponent->BindAction(EdgePressureAction, ETriggerEvent::Completed, this, &AKiteRiderPawn::OnEdgePressureTriggered);
		}
		if (LoopAction)
		{
			EnhancedInputComponent->BindAction(LoopAction, ETriggerEvent::Started, this, &AKiteRiderPawn::OnLoopStarted);
			EnhancedInputComponent->BindAction(LoopAction, ETriggerEvent::Completed, this, &AKiteRiderPawn::OnLoopCompleted);
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

void AKiteRiderPawn::ApplyScriptedInput(float Steer, float SheetRate, float Carve, float EdgePressure, bool bLoop)
{
	KeySteerInput = FMath::Clamp(Steer, -1.0f, 1.0f);
	SteerKite(KeySteerInput);
	SetSheetRateInput(SheetRate);
	EdgeBoard(Carve);
	if (BoardMovement)
	{
		BoardMovement->SetEdgePressure(EdgePressure);
	}
	bLoopKeyHeld = bLoop;
}

void AKiteRiderPawn::OnEdgePressureTriggered(const FInputActionValue& Value)
{
	if (BoardMovement)
	{
		BoardMovement->SetEdgePressure(Value.Get<float>());
	}
}

void AKiteRiderPawn::OnLoopStarted(const FInputActionValue& Value)
{
	bLoopKeyHeld = true;
}

void AKiteRiderPawn::OnLoopCompleted(const FInputActionValue& Value)
{
	bLoopKeyHeld = false;
}

void AKiteRiderPawn::UpdateMouseBar()
{
	// The mouse is the bar while the right button is held: sideways steers, towards you sheets
	// in, and the left button is the hard pull that loops the kite. Letting go centres the bar.
	const APlayerController* PC = Cast<APlayerController>(GetController());
	const bool bMouseBar = PC && PC->IsInputKeyDown(EKeys::RightMouseButton);
	bool bMouseLoop = false;
	if (bMouseBar)
	{
		float DeltaX = 0.0f;
		float DeltaY = 0.0f;
		PC->GetInputMouseDelta(DeltaX, DeltaY);
		MouseSteerInput = FMath::Clamp(MouseSteerInput + DeltaX * MouseSteerSensitivity, -1.0f, 1.0f);
		SheetKite(CurrentSheetInput - DeltaY * MouseSheetSensitivity);
		bMouseLoop = PC->IsInputKeyDown(EKeys::LeftMouseButton);
		SteerKite(KeySteerInput + MouseSteerInput);
	}
	else if (MouseSteerInput != 0.0f)
	{
		MouseSteerInput = 0.0f;
		SteerKite(KeySteerInput);
	}

	if (Kite)
	{
		Kite->SetLoopHeld(bLoopKeyHeld || bMouseLoop);
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
			UGameplayStatics::PlaySoundAtLocation(this, PopSound, GetActorLocation());
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

bool AKiteRiderPawn::HasKitePosition() const
{
	if (!Kite)
	{
		return false;
	}
	const float KiteDistance = FVector::Dist(Kite->GetKiteWorldPosition(), GetActorLocation());
	return FMath::IsNearlyEqual(KiteDistance, Kite->LineLengthCm, Kite->LineLengthCm * 0.5f);
}

void AKiteRiderPawn::UpdateRiderPose(float DeltaTime)
{
	// Face the kite, falling back to the board's heading before the kite has a position.
	float TargetFacingYawDeg = GetActorRotation().Yaw;
	float LeanDeg = 0.0f;
	if (HasKitePosition())
	{
		TargetFacingYawDeg = SmoothedKiteOffset.Rotation().Yaw;

		// Lean back against the horizontal pull of the lines.
		const float FullLeanForce = 60000.0f; // 600 N
		LeanDeg = RiderMaxLeanDeg * FMath::Clamp(Kite->GetLineForce().Size2D() / FullLeanForce, 0.0f, 1.0f);
	}

	RiderFacingYawDeg = bViewInitialized
		? FMath::FixedTurn(RiderFacingYawDeg, TargetFacingYawDeg, 240.0f * DeltaTime)
		: TargetFacingYawDeg;

	const FVector Facing = FRotator(0.0f, RiderFacingYawDeg, 0.0f).Vector();
	const float LeanRad = FMath::DegreesToRadians(LeanDeg);
	const FVector BodyUp = FVector::UpVector * FMath::Cos(LeanRad) - Facing * FMath::Sin(LeanRad);
	const FQuat BodyQuat = FRotationMatrix::MakeFromZX(BodyUp, Facing).ToQuat();

	if (RiderMesh)
	{
		// The mannequin's front is +Y in mesh space.
		RiderMesh->SetWorldRotation(BodyQuat * FQuat(FRotator(0.0f, -90.0f, 0.0f)));
	}

	if (ControlBarMesh && Kite)
	{
		const FVector BarCentre = (Kite->GetBarEndWorldPosition(true) + Kite->GetBarEndWorldPosition(false)) * 0.5f;
		ControlBarMesh->SetWorldLocationAndRotation(BarCentre, FRotator(0.0f, RiderFacingYawDeg, CurrentSteerInput * 25.0f));
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
		if (UAudioComponent* AudioComp = UGameplayStatics::SpawnSoundAtLocation(this, CrashSound, GetActorLocation(), FRotator::ZeroRotator, FMath::Clamp(Intensity, 0.2f, 1.5f)))
		{
			AudioComp->SetFloatParameter(FName("CrashIntensity"), Intensity);
		}
	}
}

void AKiteRiderPawn::HandleBoardReset()
{
	if (ResetSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ResetSound, GetActorLocation());
	}
}

void AKiteRiderPawn::HandleBoardLanding(float LandingG)
{
	if (LandingSound)
	{
		if (UAudioComponent* AudioComp = UGameplayStatics::SpawnSoundAtLocation(this, LandingSound, GetActorLocation()))
		{
			AudioComp->SetFloatParameter(FName("LandingG"), LandingG);
		}
	}
}

void AKiteRiderPawn::UpdateAudioModulation(float DeltaTime)
{
	const FVector Vel = GetBoardVelocity();
	const float BoardSpeedKnots = Vel.Size2D() / 51.44f;

	// ApparentWind = TrueWind - RiderVelocity
	const FVector TrueWind = Wind ? Wind->GetWindAt(GetActorLocation()) : FVector(20.0f * 51.44f, 0.0f, 0.0f);
	const FVector ApparentWindVec = TrueWind - Vel;
	const float ApparentWindKnots = ApparentWindVec.Size() / 51.44f;

	// Line tension in Newtons
	const float LineTensionN = Kite ? Kite->GetLineTensionN() : 0.0f;

	// Modulate continuous AudioBed MetaSound (three inputs: ApparentWind, LineTension, BoardSpeed)
	if (AudioBedComponent && AudioBedComponent->IsPlaying())
	{
		AudioBedComponent->SetFloatParameter(FName("ApparentWind"), ApparentWindKnots);
		AudioBedComponent->SetFloatParameter(FName("LineTension"), LineTensionN);
		AudioBedComponent->SetFloatParameter(FName("BoardSpeed"), BoardSpeedKnots);
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
