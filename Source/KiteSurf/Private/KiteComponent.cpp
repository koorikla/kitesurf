#include "KiteComponent.h"
#include "WindComponent.h"
#include "KiteWindMath.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "CableComponent.h"
#include "Engine/World.h"

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
	SteerSensitivity = 10.0f;
	MaxSteerRateDegPerSec = 90.0f;
	MinElevationDeg = 12.0f;
	EdgeDepthDeg = 5.0f;
	PowerDepthDeg = 9.0f;
	DepthRateDegPerSec = 15.0f;
	WindowAxisResponse = 2.0f;
	WindowRiderVelocity = FVector::ZeroVector;
	bSnapWindowRiderVelocity = true;
	MaxWindowSwingDeg = 75.0f;
	MaxKiteAirspeedCmS = 2500.0f;
	bDrawDebug = false;

	KiteWorldPosition = FVector::ZeroVector;
	KiteWorldRotation = FRotator::ZeroRotator;
	LastKitePosition = FVector::ZeroVector;
	LastKiteOffset = FVector::ZeroVector;
	KiteVelocity = FVector::ZeroVector;
	bHasLastPosition = false;

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

	FVector InitialPos, InitialDir;
	ComputeKiteTransform(InitialPos, InitialDir);
	KiteWorldPosition = InitialPos;
	LastKitePosition = InitialPos;
	bHasLastPosition = false;

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

	// Calculate orientation of the kite:
	// Heading facing along tangent of wind window + pitch/roll based on apparent wind and steering
	const FVector RiderPos = Owner->GetActorLocation();
	const FVector LineDir = (KiteWorldPosition - RiderPos).GetSafeNormal();
	
	// Forward is tangential to the sphere pointing in flight direction, Up is outwards (LineDir)
	const FVector UpVector = LineDir;
	const FVector TangentLateral = FVector::CrossProduct(FVector::UpVector, LineDir).GetSafeNormal();
	const FVector ForwardVector = FVector::CrossProduct(LineDir, TangentLateral).GetSafeNormal();
	FRotator BaseRot = FRotationMatrix::MakeFromXZ(ForwardVector, UpVector).Rotator();
	BaseRot.Roll += Steer * 25.0f; // Roll into the turn
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
	bHasLastPosition = false;
	bSnapWindowRiderVelocity = true;
}

void UKiteComponent::SetElevationDeg(float InElevationDeg)
{
	ElevationDeg = FMath::Clamp(InElevationDeg, 0.0f, 90.0f);
	bHasLastPosition = false;
	bSnapWindowRiderVelocity = true;
}

void UKiteComponent::SetWindComponent(UWindComponent* InWindComponent)
{
	WindComponent = InWindComponent;
}

namespace
{
	/** Kite direction split into clock position around the wind axis and depth into the window, both in degrees. */
	void ToClockAndDepth(float AzimuthDeg, float ElevationDeg, float& OutClockDeg, float& OutDepthDeg)
	{
		const float Azimuth = FMath::DegreesToRadians(AzimuthDeg);
		const float Elevation = FMath::DegreesToRadians(ElevationDeg);
		const float Downwind = FMath::Cos(Elevation) * FMath::Cos(Azimuth);
		const float Right = FMath::Cos(Elevation) * FMath::Sin(Azimuth);
		const float Up = FMath::Sin(Elevation);
		OutClockDeg = FMath::RadiansToDegrees(FMath::Atan2(Right, Up));
		OutDepthDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Downwind, -1.0f, 1.0f)));
	}

	void FromClockAndDepth(float ClockDeg, float DepthDeg, float& OutAzimuthDeg, float& OutElevationDeg)
	{
		const float Clock = FMath::DegreesToRadians(ClockDeg);
		const float Depth = FMath::DegreesToRadians(DepthDeg);
		const float Downwind = FMath::Sin(Depth);
		const float Right = FMath::Cos(Depth) * FMath::Sin(Clock);
		const float Up = FMath::Cos(Depth) * FMath::Cos(Clock);
		OutAzimuthDeg = FMath::RadiansToDegrees(FMath::Atan2(Right, Downwind));
		OutElevationDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Up, -1.0f, 1.0f)));
	}
}

void UKiteComponent::SetWindowPosition(float ClockDeg, float DepthDeg)
{
	float NewAzimuthDeg = 0.0f;
	float NewElevationDeg = 0.0f;
	FromClockAndDepth(FMath::Clamp(ClockDeg, -90.0f, 90.0f), FMath::Clamp(DepthDeg, 0.0f, 90.0f), NewAzimuthDeg, NewElevationDeg);
	SetAzimuthDeg(NewAzimuthDeg);
	SetElevationDeg(NewElevationDeg);
}

float UKiteComponent::GetClockDeg() const
{
	float ClockDeg = 0.0f;
	float DepthDeg = 0.0f;
	ToClockAndDepth(AzimuthDeg, ElevationDeg, ClockDeg, DepthDeg);
	return ClockDeg;
}

float UKiteComponent::GetWindowDepthDeg() const
{
	float ClockDeg = 0.0f;
	float DepthDeg = 0.0f;
	ToClockAndDepth(AzimuthDeg, ElevationDeg, ClockDeg, DepthDeg);
	return DepthDeg;
}

FVector UKiteComponent::GetDownwindDir() const
{
	const FVector RiderPos = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
	const FVector DownwindDir = GetWindAt(RiderPos).GetSafeNormal2D();
	return DownwindDir.IsNearlyZero() ? FVector::ForwardVector : DownwindDir;
}

FVector UKiteComponent::GetRiderVelocity2D() const
{
	const AActor* Owner = GetOwner();
	return Owner ? FVector(Owner->GetVelocity().X, Owner->GetVelocity().Y, 0.0f) : FVector::ZeroVector;
}

FVector UKiteComponent::GetWindowAxis() const
{
	const FVector TrueDir = GetDownwindDir();
	const FVector RiderPos = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
	const FVector TrueWind = GetWindAt(RiderPos);
	const FVector RiderVelocity = bSnapWindowRiderVelocity ? GetRiderVelocity2D() : WindowRiderVelocity;
	const FVector ApparentWind = UKiteWindMath::ApparentWind(FVector(TrueWind.X, TrueWind.Y, 0.0f), RiderVelocity);

	// How far the window has swung away from the true wind. Running downwind near wind speed leaves
	// too little apparent wind to define a direction, so the swing fades out there, and it is capped
	// so that outrunning the wind cannot flip the window behind the rider.
	const float TrueSpeed = TrueWind.Size2D();
	const float ApparentSpeed = ApparentWind.Size2D();
	if (TrueSpeed < KINDA_SMALL_NUMBER || ApparentSpeed < KINDA_SMALL_NUMBER)
	{
		return TrueDir;
	}
	const FVector ApparentDir = ApparentWind / ApparentSpeed;
	const float SwingDeg = FMath::RadiansToDegrees(FMath::Atan2(
		TrueDir.X * ApparentDir.Y - TrueDir.Y * ApparentDir.X,
		FVector::DotProduct(TrueDir, ApparentDir)));
	const float Confidence = FMath::Clamp(ApparentSpeed / (0.3f * TrueSpeed), 0.0f, 1.0f);
	const float ClampedSwingDeg = FMath::Clamp(SwingDeg, -MaxWindowSwingDeg, MaxWindowSwingDeg) * Confidence;
	return TrueDir.RotateAngleAxis(ClampedSwingDeg, FVector::UpVector);
}

FVector UKiteComponent::GetBarEndWorldPosition(bool bLeft) const
{
	const FVector RiderPos = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
	FVector TowardsKite = (KiteWorldPosition - RiderPos).GetSafeNormal2D();
	if (TowardsKite.IsNearlyZero())
	{
		TowardsKite = GetWindowAxis();
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

void UKiteComponent::ComputeKiteTransform(FVector& OutKitePos, FVector& OutLineDir) const
{
	const FVector RiderPos = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
	const FVector DownwindDir = GetWindowAxis();

	// Shared wind-window geometry lives in KiteWindMath (clamps azimuth/elevation itself).
	OutKitePos = UKiteWindMath::KitePositionInWindow(RiderPos, DownwindDir, AzimuthDeg, ElevationDeg, LineLengthCm);
	OutLineDir = (OutKitePos - RiderPos).GetSafeNormal();
	if (OutLineDir.IsNearlyZero())
	{
		OutLineDir = FVector::UpVector;
	}
}

void UKiteComponent::UpdateKite(float DeltaTime)
{
	// 0. Let the window follow the rider's velocity
	const FVector RiderVelocity = GetRiderVelocity2D();
	if (bSnapWindowRiderVelocity || DeltaTime <= 0.0f)
	{
		WindowRiderVelocity = RiderVelocity;
		bSnapWindowRiderVelocity = false;
	}
	else
	{
		WindowRiderVelocity = FMath::VInterpTo(WindowRiderVelocity, RiderVelocity, DeltaTime, WindowAxisResponse);
	}

	// 1. Initial position & wind estimate
	FVector CurrentPos, LineDir;
	ComputeKiteTransform(CurrentPos, LineDir);

	// Kite velocity: the rider's own velocity plus the kite's motion around the rider. The second
	// part comes from a position difference, so it is capped to stay sane when the kite is repositioned.
	const FVector RiderPos = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
	const FVector RiderVelocity3D = GetOwner() ? GetOwner()->GetVelocity() : FVector::ZeroVector;
	FVector CurrentOffset = CurrentPos - RiderPos;
	KiteVelocity = RiderVelocity3D;
	if (bHasLastPosition && DeltaTime > 0.0f)
	{
		KiteVelocity += ((CurrentOffset - LastKiteOffset) / DeltaTime).GetClampedToMaxSize(MaxKiteAirspeedCmS);
	}

	// Apparent wind: true wind at kite position - kite velocity
	const FVector TrueWind = GetWindAt(CurrentPos);
	const FVector ApparentWind = UKiteWindMath::ApparentWind(TrueWind, KiteVelocity);
	const float ApparentWindSpeedCmS = ApparentWind.Size();
	const float ApparentWindSpeedMps = ApparentWindSpeedCmS / 100.0f; // cm/s -> m/s

	// 2. Dynamics: steering & depower drift
	if (DeltaTime > 0.0f)
	{
		// Steering flies the kite around the wind axis like a clock hand: right from the zenith
		// down to the right horizon, left down to the left horizon. Rate scales with apparent wind.
		float ClockDeg = 0.0f;
		float DepthDeg = 0.0f;
		ToClockAndDepth(AzimuthDeg, ElevationDeg, ClockDeg, DepthDeg);

		const float SteerRate = FMath::Min(SteerSensitivity * ApparentWindSpeedMps, MaxSteerRateDegPerSec);
		ClockDeg += Steer * SteerRate * DeltaTime;

		// Sheeting in pulls the kite deeper into the window; sheeting out lets it sit at the edge.
		const float TargetDepthDeg = FMath::Lerp(EdgeDepthDeg, PowerDepthDeg, FMath::Clamp(Sheet, 0.0f, 1.0f));
		DepthDeg = FMath::FInterpConstantTo(DepthDeg, TargetDepthDeg, DeltaTime, DepthRateDegPerSec);

		// Keep the kite off the water: limit the clock angle so elevation stays above the minimum.
		const float MinUp = FMath::Sin(FMath::DegreesToRadians(MinElevationDeg));
		const float RingRadius = FMath::Cos(FMath::DegreesToRadians(DepthDeg));
		const float MaxClockDeg = RingRadius > MinUp ? FMath::RadiansToDegrees(FMath::Acos(MinUp / RingRadius)) : 0.0f;
		ClockDeg = FMath::Clamp(ClockDeg, -MaxClockDeg, MaxClockDeg);

		FromClockAndDepth(ClockDeg, DepthDeg, AzimuthDeg, ElevationDeg);
		AzimuthDeg = FMath::Clamp(AzimuthDeg, -90.0f, 90.0f);
		ElevationDeg = FMath::Clamp(ElevationDeg, 0.0f, 90.0f);

		// Recompute transform after angle update
		ComputeKiteTransform(CurrentPos, LineDir);
		CurrentOffset = CurrentPos - RiderPos;
		if (bHasLastPosition)
		{
			KiteVelocity = RiderVelocity3D + ((CurrentOffset - LastKiteOffset) / DeltaTime).GetClampedToMaxSize(MaxKiteAirspeedCmS);
		}
	}

	KiteWorldPosition = CurrentPos;
	LastKitePosition = CurrentPos;
	LastKiteOffset = CurrentOffset;
	bHasLastPosition = true;

	// 3. Aerodynamics calculations
	if (ApparentWindSpeedMps < 0.001f)
	{
		LineTensionN = 0.0f;
		LineForce = FVector::ZeroVector;
		return;
	}

	// Angle of attack from sheet. The lift coefficient reaches its 1.2 clamp at 11 deg, so power
	// rises over the first 80% of bar travel; 1.5 deg leaves a depowered kite just flying.
	const float ClampedSheet = FMath::Clamp(Sheet, 0.0f, 1.0f);
	const float AlphaDeg = FMath::Lerp(1.5f, 13.0f, ClampedSheet);
	const float AlphaRad = FMath::DegreesToRadians(AlphaDeg);

	// Cl = 2*PI*sin(alpha) clamped to 1.2
	const float Cl = FMath::Clamp(2.0f * UE_PI * FMath::Sin(AlphaRad), 0.0f, 1.2f);

	// Cd = 0.05 + Cl^2 / (PI * AR) with AR = 5
	const float AspectRatio = 5.0f;
	const float Cd = 0.05f + (Cl * Cl) / (UE_PI * AspectRatio);

	// Dynamic pressure: q = 0.5 * rho * v^2, air density = 1.225 kg/m^3
	const float AirDensity = 1.225f;
	const float Q = 0.5f * AirDensity * (ApparentWindSpeedMps * ApparentWindSpeedMps);

	const float LiftMagnitudeN = Q * AreaM2 * Cl;
	const float DragMagnitudeN = Q * AreaM2 * Cd;

	// Drag is along apparent wind
	FVector DragDir = ApparentWind.GetSafeNormal();
	if (DragDir.IsNearlyZero())
	{
		DragDir = LineDir;
	}

	// Lift is perpendicular to apparent wind, directed outward in the line-wind plane
	FVector LiftDir = LineDir - (LineDir | DragDir) * DragDir;
	if (!LiftDir.Normalize())
	{
		LiftDir = FVector::VectorPlaneProject(FVector::UpVector, DragDir).GetSafeNormal();
		if (LiftDir.IsNearlyZero())
		{
			LiftDir = FVector::VectorPlaneProject(FVector::ForwardVector, DragDir).GetSafeNormal();
		}
	}

	// Resultant aero force in Newtons
	const FVector ResultantAeroForceN = DragMagnitudeN * DragDir + LiftMagnitudeN * LiftDir;

	// Resultant projected onto the line direction gives line tension (>= 0)
	const float ProjectedTensionN = ResultantAeroForceN | LineDir;
	LineTensionN = FMath::Max(0.0f, ProjectedTensionN);

	// Force on rider (1 N = 100 kg*cm/s^2)
	LineForce = LineDir * (LineTensionN * 100.0f);

#if !UE_BUILD_SHIPPING
	if (bDrawDebug && GetWorld())
	{
		DrawDebugLine(GetWorld(), RiderPos, KiteWorldPosition, FColor::Yellow, false, -1.0f, 0, 2.0f);

		const FString DebugText = FString::Printf(TEXT("Tension: %.1f N\nForce: (%.0f, %.0f, %.0f)\nAz: %.1f, El: %.1f\nSheet: %.2f"),
			LineTensionN, LineForce.X, LineForce.Y, LineForce.Z, AzimuthDeg, ElevationDeg, Sheet);
		DrawDebugString(GetWorld(), KiteWorldPosition + FVector(0.0f, 0.0f, 50.0f), DebugText, nullptr, FColor::White, 0.0f, true);
	}
#endif
}
