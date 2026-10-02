#include "Capture/KiteSurfCinematicCamera.h"
#include "BoardMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "KiteComponent.h"
#include "KiteRiderPawn.h"

namespace
{
	FVector FlatDirection(const FVector& V, const FVector& Fallback)
	{
		const FVector Flat(V.X, V.Y, 0.0f);
		return Flat.SizeSquared() > 1.0f ? Flat.GetSafeNormal() : Fallback;
	}

	struct FShotAxes
	{
		/** Across the wind, the way the rider is going. */
		FVector Heading;
		/** Away from the kite: the side to film from to get both in frame. */
		FVector Upwind;
	};

	FShotAxes ShotAxes(const FVector& RiderLocation, const FVector& RiderVelocity, const FVector& KiteLocation)
	{
		FShotAxes Axes;
		Axes.Upwind = -FlatDirection(KiteLocation - RiderLocation, FVector::ForwardVector);
		// Square to the kite rather than along the velocity, so a rider running downwind does not put
		// the camera out among the lines.
		const FVector Across = FVector::CrossProduct(FVector::UpVector, Axes.Upwind);
		Axes.Heading = FVector::DotProduct(RiderVelocity, Across) >= 0.0f ? Across : -Across;
		return Axes;
	}
}

AKiteSurfCinematicCamera::AKiteSurfCinematicCamera()
{
	PrimaryActorTick.bCanEverTick = true;
	// After the rider has moved this frame, so the camera films where it is now.
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	RootComponent = Camera;
	Camera->bConstrainAspectRatio = false;
}

bool AKiteSurfCinematicCamera::ParseShot(const FString& Name, EKiteSurfShot& OutShot)
{
	static const TPair<const TCHAR*, EKiteSurfShot> Names[] = {
		{ TEXT("Chase"), EKiteSurfShot::Chase },
		{ TEXT("Side"), EKiteSurfShot::Side },
		{ TEXT("Low"), EKiteSurfShot::Low },
		{ TEXT("Orbit"), EKiteSurfShot::Orbit },
		{ TEXT("Wide"), EKiteSurfShot::Wide },
		{ TEXT("KiteView"), EKiteSurfShot::KiteView },
	};
	for (const TPair<const TCHAR*, EKiteSurfShot>& Entry : Names)
	{
		if (Name.Equals(Entry.Key, ESearchCase::IgnoreCase))
		{
			OutShot = Entry.Value;
			return true;
		}
	}
	return false;
}

FVector AKiteSurfCinematicCamera::ComputeShotAnchor(EKiteSurfShot InShot, const FVector& RiderLocation, const FVector& RiderVelocity, const FVector& KiteLocation, float WaterZ)
{
	if (InShot != EKiteSurfShot::Wide)
	{
		return RiderLocation;
	}
	// Far enough ahead that the rider rides into a long lens and past it, and off the rider's line
	// on the side away from the kite, so the kite never crosses the lens.
	const FShotAxes Axes = ShotAxes(RiderLocation, RiderVelocity, KiteLocation);
	const FVector Anchor = RiderLocation + Axes.Heading * 4000.0f + Axes.Upwind * 1800.0f;
	return FVector(Anchor.X, Anchor.Y, WaterZ + 250.0f);
}

FKiteSurfShotFrame AKiteSurfCinematicCamera::ComputeShotFrame(EKiteSurfShot InShot, const FVector& RiderLocation, const FVector& RiderVelocity,
	const FVector& KiteLocation, float InShotSeconds, const FVector& InShotAnchor, float WaterZ)
{
	const FShotAxes Axes = ShotAxes(RiderLocation, RiderVelocity, KiteLocation);
	const FVector RiderToKite = KiteLocation - RiderLocation;
	const FVector Up = FVector::UpVector;
	// Cameras that follow the rider stay at a height over the water, so a jump rises through the frame.
	const FVector OnWater(RiderLocation.X, RiderLocation.Y, WaterZ);

	FKiteSurfShotFrame Frame;
	switch (InShot)
	{
	case EKiteSurfShot::Side:
		Frame.Location = OnWater + Axes.Upwind * 2200.0f + Axes.Heading * 400.0f + Up * 250.0f;
		Frame.LookAt = RiderLocation + RiderToKite * 0.35f;
		Frame.FieldOfViewDeg = 60.0f;
		break;
	case EKiteSurfShot::Low:
		Frame.Location = OnWater + Axes.Heading * 1500.0f + Axes.Upwind * 800.0f + Up * 150.0f;
		Frame.LookAt = RiderLocation + RiderToKite * 0.25f;
		Frame.FieldOfViewDeg = 80.0f;
		break;
	case EKiteSurfShot::Orbit:
	{
		const float StartYawDeg = Axes.Upwind.Rotation().Yaw;
		const FRotator Around(0.0f, StartYawDeg + InShotSeconds * 12.0f, 0.0f);
		Frame.Location = OnWater + Around.Vector() * 1700.0f + Up * 250.0f;
		Frame.LookAt = RiderLocation + RiderToKite * 0.4f;
		Frame.FieldOfViewDeg = 74.0f;
		break;
	}
	case EKiteSurfShot::Wide:
		Frame.Location = InShotAnchor;
		Frame.LookAt = RiderLocation + RiderToKite * 0.3f;
		Frame.FieldOfViewDeg = 55.0f;
		break;
	case EKiteSurfShot::KiteView:
		Frame.Location = KiteLocation + RiderToKite.GetSafeNormal() * 1300.0f + Up * 400.0f;
		Frame.LookAt = RiderLocation;
		Frame.FieldOfViewDeg = 70.0f;
		break;
	case EKiteSurfShot::Chase:
	default:
		Frame.Location = OnWater - Axes.Heading * 1000.0f + Up * 300.0f;
		Frame.LookAt = RiderLocation + RiderToKite * 0.3f;
		Frame.FieldOfViewDeg = 90.0f;
		break;
	}
	return Frame;
}

void AKiteSurfCinematicCamera::StartShot(EKiteSurfShot InShot, AKiteRiderPawn* InRider)
{
	Shot = InShot;
	Rider = InRider;
	ShotSeconds = 0.0f;

	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!Rider || !PC)
	{
		return;
	}
	const UKiteComponent* Kite = Rider->GetKite();
	ShotAnchor = ComputeShotAnchor(Shot, Rider->GetActorLocation(), Rider->GetBoardVelocity(),
		Kite ? Kite->GetKiteWorldPosition() : Rider->GetActorLocation() + FVector(0.0f, 0.0f, 2000.0f), GetWaterZ());
	if (Shot == EKiteSurfShot::Chase)
	{
		PC->SetViewTarget(Rider);
		return;
	}
	UpdateFrame(0.0f, true);
	PC->SetViewTarget(this);
}

void AKiteSurfCinematicCamera::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	ShotSeconds += DeltaTime;
	if (Rider && Shot != EKiteSurfShot::Chase)
	{
		UpdateFrame(DeltaTime, false);
	}
}

float AKiteSurfCinematicCamera::GetWaterZ() const
{
	float WaterZ = 0.0f;
	FVector Normal = FVector::UpVector;
	if (const UBoardMovementComponent* Board = Rider ? Rider->GetBoardMovement() : nullptr)
	{
		Board->SampleWaterSurface(Rider->GetActorLocation(), WaterZ, Normal);
	}
	return WaterZ;
}

void AKiteSurfCinematicCamera::UpdateFrame(float DeltaTime, bool bSnap)
{
	const UKiteComponent* Kite = Rider->GetKite();
	const FVector RiderLocation = Rider->GetActorLocation();
	const FVector KiteLocation = Kite ? Kite->GetKiteWorldPosition() : RiderLocation + FVector(0.0f, 0.0f, 2000.0f);
	const FKiteSurfShotFrame Frame = ComputeShotFrame(Shot, RiderLocation, Rider->GetBoardVelocity(), KiteLocation, ShotSeconds, ShotAnchor, GetWaterZ());

	// The camera rides a little behind the action, like a hand-held or boat-mounted camera; a planted
	// shot stays put and only pans.
	if (bSnap)
	{
		SmoothedLocation = Frame.Location;
		SmoothedLookAt = Frame.LookAt;
	}
	else
	{
		SmoothedLocation = Shot == EKiteSurfShot::Wide ? Frame.Location : FMath::VInterpTo(SmoothedLocation, Frame.Location, DeltaTime, 3.0f);
		SmoothedLookAt = FMath::VInterpTo(SmoothedLookAt, Frame.LookAt, DeltaTime, 4.0f);
	}

	SetActorLocationAndRotation(SmoothedLocation, (SmoothedLookAt - SmoothedLocation).Rotation());
	Camera->SetFieldOfView(Frame.FieldOfViewDeg);
}
