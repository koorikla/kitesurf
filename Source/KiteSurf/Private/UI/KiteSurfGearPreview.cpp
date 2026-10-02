#include "UI/KiteSurfGearPreview.h"
#include "Animation/AnimationAsset.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "KiteComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	/** The camera, in front of the stand and to one side, a little low, looking at a point between rider and kite. */
	const FVector CameraTarget(0.0f, 0.0f, 230.0f);
	const FVector CameraDirection = FVector(0.45f, 0.86f, 0.10f).GetSafeNormal();
	constexpr float CameraDistance = 600.0f;
	/**
	 * The kite (cm): behind the rider and above, brought in close rather than out on its 24 m lines so
	 * rider and kite both fill the picture. It stays put while the rider turns on the stand below it.
	 */
	const FVector KiteAboveStand = FVector(0.0f, 0.0f, 450.0f) - FVector(CameraDirection.X, CameraDirection.Y, 0.0f).GetSafeNormal() * 350.0f;
	constexpr float CameraFieldOfViewDeg = 50.0f;
	/** The sky card stands this far behind the target, square to the camera, big enough to fill the picture. */
	constexpr float BackdropDistance = 1500.0f;
	/** Emissive scale that comes out at about the menu art's brightness under the camera's fixed exposure. */
	constexpr float BackdropBrightness = 0.35f;

	void MakePreviewOnly(UPrimitiveComponent* Component)
	{
		// Seen by the preview camera only: never in the game view, never colliding or casting onto the level.
		Component->SetVisibleInSceneCaptureOnly(true);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetMobility(EComponentMobility::Movable);
	}
}

AKiteSurfGearPreview::AKiteSurfGearPreview()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Stand = CreateDefaultSubobject<USceneComponent>(TEXT("Stand"));
	Stand->SetupAttachment(RootComponent);

	BoardMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Board"));
	BoardMesh->SetupAttachment(Stand);
	MakePreviewOnly(BoardMesh);

	// Across the board, as on the water (AKiteRiderPawn::UpdateRiderPose).
	RiderStaticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Rider"));
	RiderStaticMesh->SetupAttachment(Stand);
	RiderStaticMesh->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, 2.0f), FRotator(0.0f, 90.0f, 0.0f));
	MakePreviewOnly(RiderStaticMesh);

	// The kite flies behind the rider and above, leading edge up and its underside (with the struts)
	// to the camera, so the three- and five-strut kites read apart.
	KiteMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Kite"));
	KiteMesh->SetupAttachment(RootComponent);
	const FVector ToCameraFlat = FVector(CameraDirection.X, CameraDirection.Y, 0.0f).GetSafeNormal();
	const FMatrix KiteAxes = FRotationMatrix::MakeFromXZ(FVector::UpVector * 0.85f + ToCameraFlat * 0.5f, -ToCameraFlat);
	KiteMesh->SetRelativeLocationAndRotation(KiteAboveStand, KiteAxes.Rotator());
	MakePreviewOnly(KiteMesh);

	// Sky and sea behind the stand. Fixed to the camera, not the turntable.
	Backdrop = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Backdrop"));
	Backdrop->SetupAttachment(RootComponent);
	const FVector BackdropLocation = CameraTarget - CameraDirection * BackdropDistance;
	const float ViewDistance = CameraDistance + BackdropDistance;
	const float HalfWidth = ViewDistance * FMath::Tan(FMath::DegreesToRadians(CameraFieldOfViewDeg * 0.5f));
	const float HalfHeight = HalfWidth * ImageHeight / ImageWidth;
	Backdrop->SetRelativeLocationAndRotation(BackdropLocation, CameraDirection.Rotation());
	// The card is 1 m square; a margin over the picture so its edges never show.
	Backdrop->SetRelativeScale3D(FVector(1.0f, HalfWidth * 2.0f / 100.0f * 1.2f, HalfHeight * 2.0f / 100.0f * 1.2f));
	Backdrop->SetCastShadow(false);
	MakePreviewOnly(Backdrop);
	if (UStaticMesh* Card = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/SM_PreviewBackdrop")))
	{
		Backdrop->SetStaticMesh(Card);
	}

	// Lit like a photo: a warm key from the camera side and a cool rim from behind.
	KeyLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("KeyLight"));
	KeyLight->SetupAttachment(RootComponent);
	KeyLight->SetRelativeLocation(CameraTarget + CameraDirection * 450.0f + FVector(0.0f, 0.0f, 350.0f));
	KeyLight->SetIntensity(80000.0f);
	KeyLight->SetAttenuationRadius(4000.0f);
	KeyLight->SetLightColor(FLinearColor(1.0f, 0.95f, 0.88f));
	KeyLight->SetCastShadows(false);

	RimLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("RimLight"));
	RimLight->SetupAttachment(RootComponent);
	RimLight->SetRelativeLocation(FVector(-600.0f, -700.0f, 500.0f));
	RimLight->SetIntensity(40000.0f);
	RimLight->SetAttenuationRadius(4000.0f);
	RimLight->SetLightColor(FLinearColor(0.75f, 0.88f, 1.0f));
	RimLight->SetCastShadows(false);

	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Camera"));
	Capture->SetupAttachment(RootComponent);
	Capture->SetRelativeLocationAndRotation(CameraTarget + CameraDirection * CameraDistance, (-CameraDirection).Rotation());
	Capture->FOVAngle = CameraFieldOfViewDeg;
	// Only the stand: the sky behind it, nothing of the level.
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->bCaptureEveryFrame = true;
	// The gear screen also opens over a paused ride.
	Capture->PrimaryComponentTick.bTickEvenWhenPaused = true;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;
	// A fixed exposure, so the picture does not pump as the stand turns.
	Capture->PostProcessSettings.bOverride_AutoExposureMethod = true;
	Capture->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	Capture->PostProcessSettings.bOverride_AutoExposureBias = true;
	Capture->PostProcessSettings.AutoExposureBias = 11.0f;
	Capture->PostProcessSettings.bOverride_MotionBlurAmount = true;
	Capture->PostProcessSettings.MotionBlurAmount = 0.0f;
	Capture->PostProcessBlendWeight = 1.0f;

	if (UStaticMesh* Board = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/SM_KiteBoard")))
	{
		BoardMesh->SetStaticMesh(Board);
	}
}

void AKiteSurfGearPreview::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	GetRenderTarget();
	ApplyTurn();
}

UTextureRenderTarget2D* AKiteSurfGearPreview::GetRenderTarget()
{
	// Made on first use, so the gear screen can show it as soon as the stand is spawned.
	if (!RenderTarget)
	{
		RenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("GearPreviewImage"));
		RenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8_SRGB;
		RenderTarget->ClearColor = FLinearColor(0.02f, 0.06f, 0.12f, 1.0f);
		RenderTarget->InitAutoFormat(ImageWidth, ImageHeight);
		Capture->TextureTarget = RenderTarget;
		Capture->ShowOnlyActors.AddUnique(this);

		if (UMaterialInterface* Sky = Backdrop->GetMaterial(0))
		{
			UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Sky, this);
			Instance->SetScalarParameterValue(TEXT("Brightness"), BackdropBrightness);
			Backdrop->SetMaterial(0, Instance);
		}
	}
	return RenderTarget;
}

void AKiteSurfGearPreview::ShowGear(ERiderCharacter Rider, EKiteModel KiteModel, float KiteSizeM2, EBoardSize Board)
{
	if (const TCHAR* MeshPath = RiderCharacter::GetStaticMeshPath(Rider))
	{
		RiderStaticMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, MeshPath));
		RiderStaticMesh->SetVisibility(true);
	}

	KiteMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, KiteGear::GetMeshPath(KiteModel)));
	// Scaled as on the water (UKiteComponent::GetSizeScale): a bigger kite looks bigger.
	const float KiteScale = FMath::Sqrt(FMath::Max(KiteSizeM2, 1.0f) / UKiteComponent::ReferenceAreaM2);
	KiteMesh->SetRelativeScale3D(FVector(KiteScale));

	BoardMesh->SetRelativeScale3D(FVector(KiteGear::GetLengthScale(Board), 1.0f, 1.0f));
}

void AKiteSurfGearPreview::TurnBy(float DeltaYawDeg)
{
	TurnYawDeg = FRotator::NormalizeAxis(TurnYawDeg + DeltaYawDeg);
	SecondsSinceManualTurn = 0.0f;
	ApplyTurn();
}

void AKiteSurfGearPreview::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	SecondsSinceManualTurn += DeltaTime;
	if (SecondsSinceManualTurn >= AutoTurnResumeSeconds)
	{
		TurnYawDeg = FRotator::NormalizeAxis(TurnYawDeg + AutoTurnDegPerSec * DeltaTime);
		ApplyTurn();
	}
}

void AKiteSurfGearPreview::ApplyTurn()
{
	Stand->SetRelativeRotation(FRotator(0.0f, TurnYawDeg, 0.0f));
}
