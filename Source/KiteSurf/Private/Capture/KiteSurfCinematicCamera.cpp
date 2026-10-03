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

	/**
	 * Aims part of the way up the lines from the rider to the kite, but no further than keeps the rider
	 * well inside the picture: with the kite overhead, aiming at it would leave the rider below the frame.
	 */
	FVector AimUpTheLines(const FVector& CameraLocation, const FVector& RiderLocation, const FVector& RiderToKite, float Fraction, float FieldOfViewDeg)
	{
		// The picture is 16:9 and the field of view is horizontal; keep the rider in the middle 85% of its height.
		const float HalfHeightDeg = FMath::RadiansToDegrees(FMath::Atan(FMath::Tan(FMath::DegreesToRadians(FieldOfViewDeg * 0.5f)) * 9.0f / 16.0f));
		const float RiderPitchDeg = (RiderLocation - CameraLocation).Rotation().Pitch;
		for (float Step = Fraction; Step > 0.0f; Step -= 0.05f)
		{
			const FVector LookAt = RiderLocation + RiderToKite * Step;
			if (FMath::Abs((LookAt - CameraLocation).Rotation().Pitch - RiderPitchDeg) <= 0.85f * HalfHeightDeg)
			{
				return LookAt;
			}
		}
		return RiderLocation;
	}

	/** Whether a point is inside a 16:9 picture from Location looking at LookAt, with a margin (1 = the edge). */
	bool IsInPicture(const FVector& Location, const FVector& LookAt, float FieldOfViewDeg, const FVector& Point, float Margin)
	{
		const FVector Local = (LookAt - Location).Rotation().UnrotateVector(Point - Location);
		if (Local.X <= 0.0f)
		{
			return false;
		}
		const float TanHalfWidth = FMath::Tan(FMath::DegreesToRadians(FieldOfViewDeg * 0.5f)) * Margin;
		return FMath::Abs(Local.Y / Local.X) < TanHalfWidth && FMath::Abs(Local.Z / Local.X) < TanHalfWidth * 9.0f / 16.0f;
	}

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
		{ TEXT("Close"), EKiteSurfShot::Close },
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
	case EKiteSurfShot::Close:
		// On the kite's side of the rider and a little ahead, level with the board: the chest, the toe
		// edge and the hands face the lens.
		Frame.Location = RiderLocation - Axes.Upwind * 330.0f + Axes.Heading * 260.0f + Up * 40.0f;
		// Never under the water (a crash sinks the rider): rendering from below the surface lost the
		// Vulkan device in an offscreen run.
		Frame.Location.Z = FMath::Max(Frame.Location.Z, WaterZ + 80.0f);
		Frame.LookAt = RiderLocation + Up * 60.0f;
		Frame.FieldOfViewDeg = 50.0f;
		break;
	case EKiteSurfShot::Chase:
	default:
		Frame.Location = OnWater - Axes.Heading * 1000.0f + Up * 300.0f;
		Frame.LookAt = RiderLocation + RiderToKite * 0.3f;
		Frame.FieldOfViewDeg = 90.0f;
		break;
	}
	if (InShot != EKiteSurfShot::KiteView && InShot != EKiteSurfShot::Close)
	{
		// The rider first; then the kite too, by opening the lens as far as needed (to a point) and,
		// if that is not enough, by standing further back. A planted shot stays where it is.
		const float Fraction = FVector::DotProduct(Frame.LookAt - RiderLocation, RiderToKite) / FMath::Max(RiderToKite.SizeSquared(), 1.0f);
		const FVector Offset = Frame.Location - RiderLocation;
		const float StartFieldOfViewDeg = Frame.FieldOfViewDeg;
		constexpr float WidestFieldOfViewDeg = 105.0f;
		bool bKiteInPicture = false;
		for (const float Back : { 1.0f, 1.3f, 1.7f, 2.2f, 3.0f })
		{
			if (Back > 1.0f && InShot == EKiteSurfShot::Wide)
			{
				break;
			}
			// Further back, but never lower over the water than the shot was set.
			const FVector Location = RiderLocation + FVector(Offset.X * Back, Offset.Y * Back, FMath::Max(Offset.Z, Offset.Z * Back));
			for (float FieldOfViewDeg = StartFieldOfViewDeg; ; FieldOfViewDeg = FMath::Min(FieldOfViewDeg + 5.0f, WidestFieldOfViewDeg))
			{
				Frame.Location = Location;
				Frame.FieldOfViewDeg = FieldOfViewDeg;
				Frame.LookAt = AimUpTheLines(Location, RiderLocation, RiderToKite, Fraction, FieldOfViewDeg);
				bKiteInPicture = IsInPicture(Location, Frame.LookAt, FieldOfViewDeg, KiteLocation, 0.9f);
				if (bKiteInPicture || FieldOfViewDeg >= WidestFieldOfViewDeg)
				{
					break;
				}
			}
			if (bKiteInPicture)
			{
				break;
			}
		}
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

	// The framing eases between frames like a hand-held or boat-mounted camera, but relative to the
	// rider, so it keeps up with them at any speed; a planted shot stays put and only pans.
	const FVector LocationOffset = Frame.Location - RiderLocation;
	const FVector LookAtOffset = Frame.LookAt - RiderLocation;
	if (bSnap)
	{
		SmoothedLocationOffset = LocationOffset;
		SmoothedLookAtOffset = LookAtOffset;
	}
	else
	{
		SmoothedLocationOffset = FMath::VInterpTo(SmoothedLocationOffset, LocationOffset, DeltaTime, 3.0f);
		SmoothedLookAtOffset = FMath::VInterpTo(SmoothedLookAtOffset, LookAtOffset, DeltaTime, 4.0f);
	}
	SmoothedLocation = Shot == EKiteSurfShot::Wide ? Frame.Location : RiderLocation + SmoothedLocationOffset;
	SmoothedLookAt = RiderLocation + SmoothedLookAtOffset;

	SetActorLocationAndRotation(SmoothedLocation, (SmoothedLookAt - SmoothedLocation).Rotation());
	SmoothedFieldOfViewDeg = bSnap ? Frame.FieldOfViewDeg : FMath::FInterpTo(SmoothedFieldOfViewDeg, Frame.FieldOfViewDeg, DeltaTime, 3.0f);
	Camera->SetFieldOfView(SmoothedFieldOfViewDeg);
}
