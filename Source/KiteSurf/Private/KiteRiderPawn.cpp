#include "KiteRiderPawn.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "WindComponent.h"
#include "BoardMovementComponent.h"
#include "KiteComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "KiteSurf.h"

AKiteRiderPawn::AKiteRiderPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	// BoardMesh root
	BoardMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BoardMesh"));
	RootComponent = BoardMesh;

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

	CurrentSteerInput = 0.0f;
	CurrentSheetInput = 0.0f;
	KiteAzimuthDeg = 0.0f;
	BoardVelocity = FVector::ZeroVector;
}

void AKiteRiderPawn::BeginPlay()
{
	Super::BeginPlay();

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
		if (JumpAction)
		{
			EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Triggered, this, &AKiteRiderPawn::OnJumpTriggered);
		}
	}
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
		return BoardMovement->Jump();
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

	const FVector Vel = GetBoardVelocity();
	ensureAlwaysMsgf(!Vel.ContainsNaN(), TEXT("AKiteRiderPawn::Tick: BoardVelocity contains NaN or Inf: %s"), *Vel.ToString());
}
