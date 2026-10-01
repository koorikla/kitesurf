#include "KiteRiderPawn.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "WindComponent.h"
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

void AKiteRiderPawn::SteerKite(float Axis)
{
	CurrentSteerInput = FMath::Clamp(Axis, -1.0f, 1.0f);
}

void AKiteRiderPawn::SheetKite(float Amount)
{
	CurrentSheetInput = FMath::Clamp(Amount, 0.0f, 1.0f);
}

FVector AKiteRiderPawn::GetBoardVelocity() const
{
	return BoardVelocity;
}

float AKiteRiderPawn::GetKiteAzimuthDeg() const
{
	return KiteAzimuthDeg;
}

void AKiteRiderPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Placeholder physics:
	// azimuth += steer * 90°/s clamped to [-90, 90]
	KiteAzimuthDeg += CurrentSteerInput * 90.0f * DeltaTime;
	KiteAzimuthDeg = FMath::Clamp(KiteAzimuthDeg, -90.0f, 90.0f);

	FVector WindVec = Wind ? Wind->GetWindAt(GetActorLocation()) : FVector(772.0f, 0.0f, 0.0f);
	float WindSpeed = WindVec.Size();
	FVector DownwindDir = WindVec.GetSafeNormal2D();
	if (DownwindDir.IsNearlyZero())
	{
		DownwindDir = FVector::ForwardVector;
	}

	// pull = downwind dir rotated by azimuth * wind speed * sheet * 0.8
	FVector PullDir = DownwindDir.RotateAngleAxis(KiteAzimuthDeg, FVector::UpVector);
	FVector Pull = PullDir * (WindSpeed * CurrentSheetInput * 0.8f);

	// velocity += pull * dt
	BoardVelocity += Pull * DeltaTime;

	// velocity -= velocity * 0.6 * dt (drag)
	BoardVelocity -= BoardVelocity * 0.6f * DeltaTime;

	// Z locked to 0
	BoardVelocity.Z = 0.0f;

	// Rotate pawn to face velocity
	if (!BoardVelocity.IsNearlyZero(1.0f))
	{
		FRotator TargetRotation = BoardVelocity.ToOrientationRotator();
		TargetRotation.Pitch = 0.0f;
		TargetRotation.Roll = 0.0f;
		SetActorRotation(TargetRotation);
	}

	FVector NewLocation = GetActorLocation() + BoardVelocity * DeltaTime;
	NewLocation.Z = 0.0f;
	SetActorLocation(NewLocation, true);
}
