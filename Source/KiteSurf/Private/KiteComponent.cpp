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
	DepowerDriftRate = 10.0f;
	bDrawDebug = false;

	KiteWorldPosition = FVector::ZeroVector;
	KiteWorldRotation = FRotator::ZeroRotator;
	LastKitePosition = FVector::ZeroVector;
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
	const FVector LeftBarPos = RiderPos + Owner->GetActorRotation().RotateVector(FVector(40.0f, -25.0f, 100.0f));
	const FVector RightBarPos = RiderPos + Owner->GetActorRotation().RotateVector(FVector(40.0f, 25.0f, 100.0f));

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
}

void UKiteComponent::SetElevationDeg(float InElevationDeg)
{
	ElevationDeg = FMath::Clamp(InElevationDeg, 0.0f, 90.0f);
	bHasLastPosition = false;
}

void UKiteComponent::SetWindComponent(UWindComponent* InWindComponent)
{
	WindComponent = InWindComponent;
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
	const FVector TrueWind = GetWindAt(RiderPos);
	FVector DownwindDir = TrueWind.GetSafeNormal2D();
	if (DownwindDir.IsNearlyZero())
	{
		DownwindDir = FVector::ForwardVector;
	}

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
	// 1. Initial position & wind estimate
	FVector CurrentPos, LineDir;
	ComputeKiteTransform(CurrentPos, LineDir);

	// Velocity calculation
	if (bHasLastPosition && DeltaTime > 0.0f)
	{
		KiteVelocity = (CurrentPos - LastKitePosition) / DeltaTime;
	}
	else
	{
		KiteVelocity = FVector::ZeroVector;
	}

	// Apparent wind: true wind at kite position - kite velocity
	const FVector TrueWind = GetWindAt(CurrentPos);
	const FVector ApparentWind = UKiteWindMath::ApparentWind(TrueWind, KiteVelocity);
	const float ApparentWindSpeedCmS = ApparentWind.Size();
	const float ApparentWindSpeedMps = ApparentWindSpeedCmS / 100.0f; // cm/s -> m/s

	// 2. Dynamics: steering & depower drift
	if (DeltaTime > 0.0f)
	{
		// Steering moves azimuth at a rate proportional to steer input * apparent wind speed
		const float DeltaAzimuth = Steer * SteerSensitivity * ApparentWindSpeedMps * DeltaTime;
		AzimuthDeg = FMath::Clamp(AzimuthDeg + DeltaAzimuth, -90.0f, 90.0f);

		// Kite drifts to the edge of the window when sheeted out
		const float Depower = 1.0f - FMath::Clamp(Sheet, 0.0f, 1.0f);
		if (Depower > 0.0f)
		{
			// Zenith edge drift (towards 85 degrees)
			const float TargetElev = 85.0f;
			if (ElevationDeg < TargetElev)
			{
				ElevationDeg = FMath::Min(ElevationDeg + Depower * DepowerDriftRate * DeltaTime, TargetElev);
			}

			// Lateral edge drift if already angled away from center
			if (FMath::Abs(AzimuthDeg) > 5.0f)
			{
				const float TargetAz = FMath::Sign(AzimuthDeg) * 85.0f;
				AzimuthDeg = FMath::FInterpTo(AzimuthDeg, TargetAz, DeltaTime, Depower * 0.5f);
			}
		}

		// Recompute transform after angle update
		ComputeKiteTransform(CurrentPos, LineDir);
		if (bHasLastPosition)
		{
			KiteVelocity = (CurrentPos - LastKitePosition) / DeltaTime;
		}
	}

	KiteWorldPosition = CurrentPos;
	LastKitePosition = CurrentPos;
	bHasLastPosition = true;

	// 3. Aerodynamics calculations
	if (ApparentWindSpeedMps < 0.001f)
	{
		LineTensionN = 0.0f;
		LineForce = FVector::ZeroVector;
		return;
	}

	// Angle of attack from sheet (depower: 4 deg .. 18 deg)
	const float ClampedSheet = FMath::Clamp(Sheet, 0.0f, 1.0f);
	const float AlphaDeg = FMath::Lerp(4.0f, 18.0f, ClampedSheet);
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
		const FVector RiderPos = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
		DrawDebugLine(GetWorld(), RiderPos, KiteWorldPosition, FColor::Yellow, false, -1.0f, 0, 2.0f);

		const FString DebugText = FString::Printf(TEXT("Tension: %.1f N\nForce: (%.0f, %.0f, %.0f)\nAz: %.1f, El: %.1f\nSheet: %.2f"),
			LineTensionN, LineForce.X, LineForce.Y, LineForce.Z, AzimuthDeg, ElevationDeg, Sheet);
		DrawDebugString(GetWorld(), KiteWorldPosition + FVector(0.0f, 0.0f, 50.0f), DebugText, nullptr, FColor::White, 0.0f, true);
	}
#endif
}
