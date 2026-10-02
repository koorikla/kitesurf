#include "KiteRiderPawn.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "WindComponent.h"
#include "BoardMovementComponent.h"
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

	// CameraBoom (800cm, -20 deg pitch, camera lag)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 800.0f;
	CameraBoom->SetRelativeRotation(FRotator(-20.0f, 0.0f, 0.0f));
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 3.0f;
	CameraBoom->bUsePawnControlRotation = false;

	// FollowCamera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// Wind component
	Wind = CreateDefaultSubobject<UWindComponent>(TEXT("Wind"));

	// Board movement component
	BoardMovement = CreateDefaultSubobject<UBoardMovementComponent>(TEXT("BoardMovement"));
	BoardMovement->UpdatedComponent = RootComponent;

	// Kite component: aerodynamics producing the line force consumed by BoardMovement
	Kite = CreateDefaultSubobject<UKiteComponent>(TEXT("Kite"));

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
	SteerKite(Value.Get<float>());
}

void AKiteRiderPawn::OnSheetTriggered(const FInputActionValue& Value)
{
	SheetKite(Value.Get<float>());
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

	// Update ControlBar rotation to reflect steering angle
	if (ControlBarMesh)
	{
		ControlBarMesh->SetRelativeRotation(FRotator(0.0f, CurrentSteerInput * 30.0f, 0.0f));
	}
	const FVector Vel = GetBoardVelocity();
	ensureAlwaysMsgf(!Vel.ContainsNaN(), TEXT("AKiteRiderPawn::Tick: BoardVelocity contains NaN or Inf: %s"), *Vel.ToString());

	UpdateAudioModulation(DeltaTime);
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
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (AKiteSurfHUD* HUD = Cast<AKiteSurfHUD>(PC->GetHUD()))
		{
			HUD->TogglePauseMenu();
		}
	}
}
