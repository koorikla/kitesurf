#include "KiteComponent.h"
#include "WindComponent.h"
#include "KiteWindMath.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "CableComponent.h"
#include "Engine/World.h"

namespace
{
	const float AirDensityKgM3 = 1.225f;
	// Bar travel below this counts as centred: the kite parks itself.
	const float CentredBarThreshold = 0.05f;
	// The turn counter restarts once the bar has been centred this long.
	const float TurnResetSeconds = 0.75f;

	/** Unit tangent at Dir that points most nearly along Preferred; falls back to a stable choice at the poles. */
	FVector TangentTowards(const FVector& Dir, const FVector& Preferred)
	{
		FVector Tangent = Preferred - FVector::DotProduct(Preferred, Dir) * Dir;
		if (!Tangent.Normalize())
		{
			Tangent = FVector::UpVector - FVector::DotProduct(FVector::UpVector, Dir) * Dir;
			if (!Tangent.Normalize())
			{
				Tangent = FVector::ForwardVector - FVector::DotProduct(FVector::ForwardVector, Dir) * Dir;
				Tangent.Normalize();
			}
		}
		return Tangent;
	}
}

UKiteComponent::UKiteComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	AzimuthDeg = 0.0f;
	ElevationDeg = 45.0f;
	LineLengthCm = 2400.0f; // 24 m
	AreaM2 = 12.0f;
	MassKg = 3.0f;
	Sheet = 0.0f;
	Steer = 0.0f;
	GlideRatioSheetedOut = 7.0f;
	GlideRatioSheetedIn = 5.5f;
	MinTurnRadiusCm = 400.0f;
	MaxAirspeedCmS = 1800.0f;
	AirspeedResponse = 2.0f;
	TravelHeadingDeg = 100.0f;
	SteerAssistGain = 6.0f;
	SteerAssistMaxRateDegPerSec = 200.0f;
	MinElevationDeg = 10.0f;
	MaxLineTensionN = 3000.0f;
	bDrawDebug = false;

	KiteDir = FVector(FMath::Cos(FMath::DegreesToRadians(45.0f)), 0.0f, FMath::Sin(FMath::DegreesToRadians(45.0f)));
	KiteHeading = FVector::UpVector;
	AirspeedCmS = 0.0f;
	TurnDeg = 0.0f;
	CentredBarSeconds = 0.0f;
	bPlacementPending = true;
	bLoopHeld = false;

	KiteWorldPosition = FVector::ZeroVector;
	KiteWorldRotation = FRotator::ZeroRotator;
	KiteVelocity = FVector::ZeroVector;

	LineTensionN = 0.0f;
	LineForce = FVector::ZeroVector;

	KiteMesh = nullptr;
	LeftLine = nullptr;
	RightLine = nullptr;
}

void UKiteComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* Owner = GetOwner())
	{
		WindComponent = Owner->FindComponentByClass<UWindComponent>();
		if (WindComponent)
		{
			AddTickPrerequisiteComponent(WindComponent);
		}
	}

	UpdateKite(0.0f);
	SetupVisuals();
}

void UKiteComponent::SetupVisuals()
{
	AActor* Owner = GetOwner();
	if (!Owner || !GetWorld())
	{
		return;
	}

	// 1. Kite Static Mesh Component
	if (!KiteMesh)
	{
		KiteMesh = NewObject<UStaticMeshComponent>(Owner, TEXT("KiteVisualMesh"));
		if (KiteMesh)
		{
			KiteMesh->RegisterComponent();
			KiteMesh->SetMobility(EComponentMobility::Movable);
			KiteMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			UStaticMesh* SM_Kite = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, TEXT("/Game/Meshes/SM_Kite")));
			if (SM_Kite)
			{
				KiteMesh->SetStaticMesh(SM_Kite);
			}
		}
	}

	// 2. Left and Right Kite Line Cables
	if (!LeftLine)
	{
		LeftLine = NewObject<UCableComponent>(Owner, TEXT("KiteLineLeft"));
		if (LeftLine)
		{
			LeftLine->RegisterComponent();
			LeftLine->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
			LeftLine->CableWidth = 2.0f;
			LeftLine->NumSegments = 10;
			LeftLine->SolverIterations = 1;
			LeftLine->bEnableStiffness = true;
			LeftLine->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			UMaterialInterface* LineMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, TEXT("/Game/Materials/M_KiteLines")));
			if (LineMat)
			{
				LeftLine->SetMaterial(0, LineMat);
			}
		}
	}

	if (!RightLine)
	{
		RightLine = NewObject<UCableComponent>(Owner, TEXT("KiteLineRight"));
		if (RightLine)
		{
			RightLine->RegisterComponent();
			RightLine->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
			RightLine->CableWidth = 2.0f;
			RightLine->NumSegments = 10;
			RightLine->SolverIterations = 1;
			RightLine->bEnableStiffness = true;
			RightLine->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			UMaterialInterface* LineMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, TEXT("/Game/Materials/M_KiteLines")));
			if (LineMat)
			{
				RightLine->SetMaterial(0, LineMat);
			}
		}
	}

	UpdateVisuals();
}

void UKiteComponent::UpdateVisuals()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// The canopy faces out along the lines with its leading edge towards the kite's heading,
	// banked into the turn.
	FRotator BaseRot = FRotationMatrix::MakeFromXZ(KiteHeading, KiteDir).Rotator();
	BaseRot.Roll += Steer * 25.0f;
	KiteWorldRotation = BaseRot;

	if (KiteMesh)
	{
		KiteMesh->SetWorldLocationAndRotation(KiteWorldPosition, KiteWorldRotation);
	}

	// Update line attachments: connecting from rider control bar to kite wing tips
	const FVector LeftBarPos = GetBarEndWorldPosition(true);
	const FVector RightBarPos = GetBarEndWorldPosition(false);

	const FVector LeftTipPos = KiteWorldPosition + KiteWorldRotation.RotateVector(FVector(0.0f, -200.0f, -40.0f));
	const FVector RightTipPos = KiteWorldPosition + KiteWorldRotation.RotateVector(FVector(0.0f, 200.0f, -40.0f));

	if (LeftLine)
	{
		LeftLine->SetWorldLocation(LeftBarPos);
		LeftLine->EndLocation = LeftLine->GetComponentTransform().InverseTransformPosition(LeftTipPos);
		LeftLine->CableLength = (LeftTipPos - LeftBarPos).Size() * 0.98f;
	}

	if (RightLine)
	{
		RightLine->SetWorldLocation(RightBarPos);
		RightLine->EndLocation = RightLine->GetComponentTransform().InverseTransformPosition(RightTipPos);
		RightLine->CableLength = (RightTipPos - RightBarPos).Size() * 0.98f;
	}
}

void UKiteComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateKite(DeltaTime);
	UpdateVisuals();
}

void UKiteComponent::SteerKite(float Axis)
{
	Steer = FMath::Clamp(Axis, -1.0f, 1.0f);
}

void UKiteComponent::SetLoopHeld(bool bHeld)
{
	bLoopHeld = bHeld;
}

void UKiteComponent::SheetKite(float Amount)
{
	Sheet = FMath::Clamp(Amount, 0.0f, 1.0f);
}

FVector UKiteComponent::GetLineForce() const
{
	return LineForce;
}

float UKiteComponent::GetLineTensionN() const
{
	return LineTensionN;
}

FVector UKiteComponent::GetKiteWorldPosition() const
{
	return KiteWorldPosition;
}

float UKiteComponent::GetAzimuthDeg() const
{
	return AzimuthDeg;
}

float UKiteComponent::GetElevationDeg() const
{
	return ElevationDeg;
}

FVector UKiteComponent::GetKiteVelocity() const
{
	return KiteVelocity;
}

FRotator UKiteComponent::GetKiteRotation() const
{
	return KiteWorldRotation;
}

void UKiteComponent::SetAzimuthDeg(float InAzimuthDeg)
{
	AzimuthDeg = FMath::Clamp(InAzimuthDeg, -90.0f, 90.0f);
	bPlacementPending = true;
}

void UKiteComponent::SetElevationDeg(float InElevationDeg)
{
	ElevationDeg = FMath::Clamp(InElevationDeg, 0.0f, 90.0f);
	bPlacementPending = true;
}

void UKiteComponent::SetWindComponent(UWindComponent* InWindComponent)
{
	WindComponent = InWindComponent;
}

void UKiteComponent::SetWindowPosition(float ClockDeg, float DepthDeg)
{
	const float Clock = FMath::DegreesToRadians(FMath::Clamp(ClockDeg, -90.0f, 90.0f));
	const float Depth = FMath::DegreesToRadians(FMath::Clamp(DepthDeg, 0.0f, 90.0f));
	const float Downwind = FMath::Sin(Depth);
	const float Right = FMath::Cos(Depth) * FMath::Sin(Clock);
	const float Up = FMath::Cos(Depth) * FMath::Cos(Clock);
	SetAzimuthDeg(FMath::RadiansToDegrees(FMath::Atan2(Right, Downwind)));
	SetElevationDeg(FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Up, -1.0f, 1.0f))));
}

float UKiteComponent::GetClockDeg() const
{
	const FVector Axis = GetWindowAxis();
	const FVector AxisRight = FVector::CrossProduct(FVector::UpVector, Axis);
	return FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(KiteDir, AxisRight), KiteDir.Z));
}

float UKiteComponent::GetWindowDepthDeg() const
{
	return FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FVector::DotProduct(KiteDir, GetWindowAxis()), -1.0f, 1.0f)));
}

FVector UKiteComponent::GetDownwindDir() const
{
	const FVector DownwindDir = GetWindAt(GetRiderPosition()).GetSafeNormal2D();
	return DownwindDir.IsNearlyZero() ? FVector::ForwardVector : DownwindDir;
}

FVector UKiteComponent::GetWindowAxis() const
{
	const FVector TrueWind = GetWindAt(GetRiderPosition());
	const FVector ApparentWind = UKiteWindMath::ApparentWind(TrueWind, GetRiderVelocity());
	const FVector Axis = ApparentWind.GetSafeNormal2D();
	return Axis.IsNearlyZero() ? GetDownwindDir() : Axis;
}

FVector UKiteComponent::GetBarEndWorldPosition(bool bLeft) const
{
	const FVector RiderPos = GetRiderPosition();
	FVector TowardsKite = (KiteWorldPosition - RiderPos).GetSafeNormal2D();
	if (TowardsKite.IsNearlyZero())
	{
		TowardsKite = GetDownwindDir();
	}
	const FVector BarRight = FVector::CrossProduct(FVector::UpVector, TowardsKite);
	return RiderPos + TowardsKite * 40.0f + FVector(0.0f, 0.0f, 100.0f) + BarRight * (bLeft ? -25.0f : 25.0f);
}

FVector UKiteComponent::GetWindAt(const FVector& Location) const
{
	if (WindComponent)
	{
		return WindComponent->GetWindAt(Location);
	}
	if (const AActor* Owner = GetOwner())
	{
		if (const UWindComponent* FoundWind = Owner->FindComponentByClass<UWindComponent>())
		{
			return FoundWind->GetWindAt(Location);
		}
	}
	return FVector::ZeroVector;
}

FVector UKiteComponent::GetRiderPosition() const
{
	return GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
}

FVector UKiteComponent::GetRiderVelocity() const
{
	return GetOwner() ? GetOwner()->GetVelocity() : FVector::ZeroVector;
}

FVector UKiteComponent::DirectionFromAngles(float InAzimuthDeg, float InElevationDeg) const
{
	return (UKiteWindMath::KitePositionInWindow(FVector::ZeroVector, GetDownwindDir(), InAzimuthDeg, InElevationDeg, 1.0f)).GetSafeNormal();
}

void UKiteComponent::PlaceParked()
{
	KiteDir = DirectionFromAngles(AzimuthDeg, ElevationDeg);

	// Parked means the kite's own airspeed exactly cancels the wind blowing across the lines,
	// which needs the nose pointing straight out of the window.
	const FVector ApparentWind = GetWindAt(GetRiderPosition() + KiteDir * LineLengthCm) - GetRiderVelocity();
	const FVector CrossLineWind = ApparentWind - FVector::DotProduct(ApparentWind, KiteDir) * KiteDir;
	KiteHeading = TangentTowards(KiteDir, -CrossLineWind);
	AirspeedCmS = FMath::Min(CrossLineWind.Size(), MaxAirspeedCmS);
	TurnDeg = 0.0f;
	CentredBarSeconds = 0.0f;
	bPlacementPending = false;
}

void UKiteComponent::UpdateAngles()
{
	const FVector Downwind = GetDownwindDir();
	const FVector Crosswind = FVector::CrossProduct(FVector::UpVector, Downwind);
	ElevationDeg = FMath::Clamp(FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(KiteDir.Z, -1.0f, 1.0f))), 0.0f, 90.0f);
	AzimuthDeg = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(KiteDir, Crosswind), FVector::DotProduct(KiteDir, Downwind))), -90.0f, 90.0f);
}

void UKiteComponent::UpdateKite(float DeltaTime)
{
	if (bPlacementPending)
	{
		PlaceParked();
	}

	const FVector RiderPos = GetRiderPosition();
	const FVector RiderVelocity = GetRiderVelocity();
	const float ClampedSheet = FMath::Clamp(Sheet, 0.0f, 1.0f);
	const float GlideRatio = FMath::Lerp(GlideRatioSheetedOut, GlideRatioSheetedIn, ClampedSheet);

	// Wind the kite feels before its own flight: true wind at the kite minus the rider's motion,
	// split into the part along the lines (which powers the kite) and the part across them.
	FVector ApparentWind = GetWindAt(RiderPos + KiteDir * LineLengthCm) - RiderVelocity;
	float AlongLineWind = FVector::DotProduct(ApparentWind, KiteDir);
	FVector CrossLineWind = ApparentWind - AlongLineWind * KiteDir;

	FVector KiteMotion = FVector::ZeroVector; // velocity of the kite around the rider
	if (DeltaTime > 0.0f)
	{
		// 1. Airspeed: the kite accelerates until its drag angle matches the wind along the lines.
		const float TargetAirspeed = FMath::Clamp(GlideRatio * FMath::Max(AlongLineWind, 0.0f), 0.0f, MaxAirspeedCmS);
		AirspeedCmS = FMath::FInterpTo(AirspeedCmS, TargetAirspeed, DeltaTime, AirspeedResponse);

		// 2. Heading. Steering normally asks for a direction of travel: bar centred means nose out
		// of the window (parked), bar over means along the window edge that way, so the kite
		// travels round the window and stops when the bar is centred. With the loop input held the
		// bar turns the kite directly, at a rate set by its airspeed, and holding it flies a loop.
		float TurnRad = 0.0f;
		const bool bSteering = FMath::Abs(Steer) >= CentredBarThreshold;
		if (bLoopHeld && bSteering)
		{
			TurnRad = Steer * (AirspeedCmS / FMath::Max(MinTurnRadiusCm, 1.0f)) * DeltaTime;
			TurnDeg += FMath::RadiansToDegrees(TurnRad);
			CentredBarSeconds = 0.0f;
		}
		else
		{
			CentredBarSeconds += DeltaTime;
			if (CentredBarSeconds >= TurnResetSeconds)
			{
				TurnDeg = 0.0f;
			}

			const float MinCrossWindCmS = 50.0f; // too little cross wind to say which way is out
			if (CrossLineWind.SizeSquared() > FMath::Square(MinCrossWindCmS))
			{
				const FVector Outward = TangentTowards(KiteDir, -CrossLineWind);
				const float TravelRad = FMath::DegreesToRadians(Steer * TravelHeadingDeg);
				FVector TargetHeading = Outward * FMath::Cos(TravelRad) + FVector::CrossProduct(Outward, KiteDir) * FMath::Sin(TravelRad);

				// At the bottom of the window there is nowhere further to travel: park instead of
				// skimming along the water into the middle of the window.
				const float FloorUp = FMath::Sin(FMath::DegreesToRadians(MinElevationDeg + 1.0f));
				if (KiteDir.Z <= FloorUp && TargetHeading.Z < 0.0f)
				{
					TargetHeading = Outward;
				}

				// Signed angle from the heading to the target, positive to the right.
				const float ErrorRad = FMath::Atan2(
					FVector::DotProduct(FVector::CrossProduct(KiteHeading, KiteDir), TargetHeading),
					FVector::DotProduct(KiteHeading, TargetHeading));
				const float MaxStepRad = FMath::DegreesToRadians(SteerAssistMaxRateDegPerSec) * DeltaTime;
				TurnRad = FMath::Clamp(ErrorRad * FMath::Clamp(SteerAssistGain * DeltaTime, 0.0f, 1.0f), -MaxStepRad, MaxStepRad);
			}
		}
		// A positive turn swings the nose to the rider's right as they look up the lines at the kite.
		KiteHeading = KiteHeading * FMath::Cos(TurnRad) + FVector::CrossProduct(KiteHeading, KiteDir) * FMath::Sin(TurnRad);

		// 3. Position: fly along the heading, drift with the wind across the lines.
		KiteMotion = AirspeedCmS * KiteHeading + CrossLineWind;
		FVector NewDir = (KiteDir + KiteMotion * (DeltaTime / LineLengthCm)).GetSafeNormal();

		// Keep the kite off the water: hold it at the minimum elevation and level its nose.
		const float MinUp = FMath::Sin(FMath::DegreesToRadians(MinElevationDeg));
		if (NewDir.Z < MinUp)
		{
			FVector Horizontal = NewDir.GetSafeNormal2D();
			if (Horizontal.IsNearlyZero())
			{
				Horizontal = GetDownwindDir();
			}
			NewDir = Horizontal * FMath::Sqrt(1.0f - MinUp * MinUp) + FVector::UpVector * MinUp;
			KiteHeading.Z = FMath::Max(KiteHeading.Z, 0.0f);
		}

		KiteMotion = (NewDir - KiteDir) * (LineLengthCm / DeltaTime);
		KiteDir = NewDir;
		KiteHeading = TangentTowards(KiteDir, KiteHeading);

		// Refresh the wind for the force at the new position.
		ApparentWind = GetWindAt(RiderPos + KiteDir * LineLengthCm) - RiderVelocity;
		AlongLineWind = FVector::DotProduct(ApparentWind, KiteDir);
		CrossLineWind = ApparentWind - AlongLineWind * KiteDir;
	}

	KiteWorldPosition = RiderPos + KiteDir * LineLengthCm;
	KiteVelocity = RiderVelocity + KiteMotion;
	UpdateAngles();

	// 4. Line tension from the air flowing over the kite: its own airspeed plus the wind along the lines.
	const float FlowSpeedSquaredM2S2 = (FMath::Square(AirspeedCmS) + FMath::Square(FMath::Max(AlongLineWind, 0.0f))) / 10000.0f;
	if (ApparentWind.IsNearlyZero(0.1f))
	{
		LineTensionN = 0.0f;
		LineForce = FVector::ZeroVector;
		return;
	}

	// Angle of attack from sheet. The lift coefficient reaches its 1.2 clamp at 11 deg, so power
	// rises over the first 80% of bar travel; 1.5 deg leaves a depowered kite just flying.
	const float AlphaRad = FMath::DegreesToRadians(FMath::Lerp(1.5f, 13.0f, ClampedSheet));
	const float LiftCoefficient = FMath::Clamp(2.0f * UE_PI * FMath::Sin(AlphaRad), 0.0f, 1.2f);
	const float DynamicPressurePa = 0.5f * AirDensityKgM3 * FlowSpeedSquaredM2S2;

	LineTensionN = FMath::Min(DynamicPressurePa * AreaM2 * LiftCoefficient, MaxLineTensionN);

	// Force on rider (1 N = 100 kg*cm/s^2)
	LineForce = KiteDir * (LineTensionN * 100.0f);

#if !UE_BUILD_SHIPPING
	if (bDrawDebug && GetWorld())
	{
		DrawDebugLine(GetWorld(), RiderPos, KiteWorldPosition, FColor::Yellow, false, -1.0f, 0, 2.0f);
		DrawDebugLine(GetWorld(), KiteWorldPosition, KiteWorldPosition + KiteHeading * 300.0f, FColor::Red, false, -1.0f, 0, 2.0f);

		const FString DebugText = FString::Printf(TEXT("Tension: %.1f N\nAirspeed: %.1f m/s\nAz: %.1f, El: %.1f\nSheet: %.2f"),
			LineTensionN, AirspeedCmS / 100.0f, AzimuthDeg, ElevationDeg, Sheet);
		DrawDebugString(GetWorld(), KiteWorldPosition + FVector(0.0f, 0.0f, 50.0f), DebugText, nullptr, FColor::White, 0.0f, true);
	}
#endif
}
