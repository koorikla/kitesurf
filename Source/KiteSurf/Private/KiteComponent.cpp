#include "KiteComponent.h"
#include "WindComponent.h"
#include "KiteWindMath.h"
#include "KiteSurf.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "CableComponent.h"
#include "Engine/World.h"
#include "KiteSurfUnits.h"

namespace
{
	constexpr float AirDensityKgM3 = KiteUnits::AirDensityKgM3;
	constexpr float GravityMS2 = KiteUnits::GravityMS2;
	constexpr float CmPerM = KiteUnits::CmPerM;
	// Bar travel below this counts as centred.
	const float CentredBarThreshold = 0.05f;
	// The loop counter restarts once the bar has been centred this long.
	const float TurnResetSeconds = 0.75f;
	// Lines count as at full length within this (cm).
	const float TautSlackCm = 5.0f;

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

	/** Signed angle in radians from From to To about Axis, positive when To is to the right of From as seen looking along Axis from the rider. */
	float SignedAngleRightRad(const FVector& From, const FVector& To, const FVector& Axis)
	{
		return FMath::Atan2(FVector::DotProduct(FVector::CrossProduct(From, Axis), To), FVector::DotProduct(From, To));
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

	AddedMassKg = 3.0f;
	MaxLiftCoefficient = 1.2f;
	StallAngleDeg = 18.0f;
	StalledForwardTiltDeg = 10.0f;
	ParasiteDragCoefficient = 0.09f;
	InducedDragFactor = 0.085f;
	SteeringDragCoefficient = 0.06f;
	SideForceCoefficient = 1.2f;
	SlackDragCoefficient = 0.7f;
	SlackCollapseCm = 150.0f;
	TrimSheetedOutDeg = -22.0f; // bar right out: the kite flags and barely pulls
	TrimSheetedInDeg = 2.0f;    // bar right in: full power, a few degrees short of the stall

	MinTurnRadiusCm = 420.0f;
	TurnResponse = 9.0f;
	WeathercockGain = 0.2f;
	GravityTurnGain = 0.15f;
	TravelHeadingDeg = 100.0f;
	SteerAssistGain = 2.5f;
	ParkHoldGain = 2.0f;
	ParkHoldMaxDeg = 45.0f;
	MinElevationDeg = 10.0f;
	CrashHeightCm = 60.0f;
	RelaunchDelaySeconds = 3.0f;
	MinRelaunchWindCmS = 350.0f;
	MaxLineTensionN = 6000.0f;
	bDrawDebug = false;
	MaxStepSeconds = 1.0f / 240.0f;
	MaxStepsPerUpdate = 48;

	KiteDir = FVector(FMath::Cos(FMath::DegreesToRadians(45.0f)), 0.0f, FMath::Sin(FMath::DegreesToRadians(45.0f)));
	KiteHeading = FVector::UpVector;
	KiteVelocity = FVector::ZeroVector;
	KiteWorldPosition = FVector::ZeroVector;
	KiteWorldRotation = FRotator::ZeroRotator;

	AirspeedCmS = 0.0f;
	AngleOfAttackDeg = 0.0f;
	TurnDeg = 0.0f;
	CentredBarSeconds = 0.0f;
	ParkClockDeg = 0.0f;
	TurnRateRadS = 0.0f;
	bHasParkClock = false;
	bPlacementPending = true;
	bLoopHeld = false;
	bLooping = false;
	LoopSide = 0.0f;
	LoopClockDeg = 35.0f;
	bCrashed = false;
	bLinesTaut = true;
	PrevKiteWorldPosition = FVector::ZeroVector;
	SimTimeSeconds = 0.0f;
	CrashedSeconds = 0.0f;

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
			KiteMesh->SetWorldScale3D(FVector(GetSizeScale()));
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

void UKiteComponent::UpdateVisuals(float Alpha)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// Drawn between the last two simulation states, so the canopy moves smoothly however the
	// frame rate divides the fixed step.
	const FVector DrawPosition = PrevKiteWorldPosition.IsZero() ? KiteWorldPosition : FMath::Lerp(PrevKiteWorldPosition, KiteWorldPosition, FMath::Clamp(Alpha, 0.0f, 1.0f));

	// The canopy faces out along the lines with its leading edge towards the kite's heading,
	// banked into the turn.
	FRotator BaseRot = FRotationMatrix::MakeFromXZ(KiteHeading, KiteDir).Rotator();
	BaseRot.Roll += Steer * 25.0f;
	KiteWorldRotation = BaseRot;

	if (KiteMesh)
	{
		KiteMesh->SetWorldLocationAndRotation(DrawPosition, KiteWorldRotation);
	}

	// Update line attachments: connecting from rider control bar to kite wing tips
	const FVector LeftBarPos = GetBarEndWorldPosition(true);
	const FVector RightBarPos = GetBarEndWorldPosition(false);

	// The trailing corner of each wingtip, as built by generate_mesh_objs.kite_wingtip().
	const float SizeScale = GetSizeScale();
	const FVector LeftTipPos = DrawPosition + KiteWorldRotation.RotateVector(FVector(-150.0f, -223.0f, -178.0f) * SizeScale);
	const FVector RightTipPos = DrawPosition + KiteWorldRotation.RotateVector(FVector(-150.0f, 223.0f, -178.0f) * SizeScale);

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
	AzimuthDeg = FMath::Clamp(InAzimuthDeg, -180.0f, 180.0f);
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
	// The window is the one the rider feels: its axis is the apparent wind, so the same clock and
	// depth put the kite in flyable air whether the rider is standing or riding.
	const float Clock = FMath::DegreesToRadians(FMath::Clamp(ClockDeg, -90.0f, 90.0f));
	const float Depth = FMath::DegreesToRadians(FMath::Clamp(DepthDeg, 0.0f, 90.0f));
	const FVector Axis = GetWindowAxis();
	const FVector AxisRight = FVector::CrossProduct(FVector::UpVector, Axis);
	const FVector Dir = Axis * FMath::Sin(Depth) + (AxisRight * FMath::Sin(Clock) + FVector::UpVector * FMath::Cos(Clock)) * FMath::Cos(Depth);

	// Stored as azimuth and elevation from the true wind, which is what PlaceParked rebuilds from.
	const FVector Downwind = GetDownwindDir();
	const FVector Crosswind = FVector::CrossProduct(FVector::UpVector, Downwind);
	AzimuthDeg = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Dir, Crosswind), FVector::DotProduct(Dir, Downwind)));
	ElevationDeg = FMath::Clamp(FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Dir.Z, -1.0f, 1.0f))), 0.0f, 90.0f);
	bPlacementPending = true;
}

float UKiteComponent::GetClockDeg() const
{
	// A kite that has been placed but not yet flown is where it was placed.
	const FVector Dir = bPlacementPending ? DirectionFromAngles(AzimuthDeg, ElevationDeg) : KiteDir;
	const FVector Axis = GetWindowAxis();
	const FVector AxisRight = FVector::CrossProduct(FVector::UpVector, Axis);
	return FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Dir, AxisRight), Dir.Z));
}

float UKiteComponent::GetWindowDepthDeg() const
{
	const FVector Dir = bPlacementPending ? DirectionFromAngles(AzimuthDeg, ElevationDeg) : KiteDir;
	return FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FVector::DotProduct(Dir, GetWindowAxis()), -1.0f, 1.0f)));
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

TConstArrayView<float> UKiteComponent::GetKiteSizesM2()
{
	static const float Sizes[] = { 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 12.0f, 14.0f, 17.0f };
	return Sizes;
}

float UKiteComponent::RecommendKiteSizeM2(float WindKnots, float RiderMassKg)
{
	// The rule of thumb riders use: area = 2.2 x weight / wind, then the nearest kite in the bag.
	const float IdealAreaM2 = 2.2f * FMath::Max(RiderMassKg, 1.0f) / FMath::Max(WindKnots, 1.0f);
	float Best = GetKiteSizesM2()[0];
	for (const float Size : GetKiteSizesM2())
	{
		if (FMath::Abs(Size - IdealAreaM2) < FMath::Abs(Best - IdealAreaM2))
		{
			Best = Size;
		}
	}
	return Best;
}

void UKiteComponent::SetKiteSize(float InAreaM2)
{
	AreaM2 = FMath::Clamp(InAreaM2, 1.0f, 25.0f);
	const float AreaRatio = AreaM2 / ReferenceAreaM2;
	const float Scale = GetSizeScale();
	const FKiteModelTraits Traits = KiteGear::GetTraits(KiteModel);
	// Reference 12 m^2 loop kite: 3 kg, 3 kg of air to push, 4.2 m turning radius, lift
	// coefficient up to 1.2, induced drag factor 0.085.
	MassKg = 3.0f * AreaRatio * Traits.MassScale;
	AddedMassKg = 3.0f * AreaRatio * Scale;
	MinTurnRadiusCm = 420.0f * Scale * Traits.TurnRadiusScale;
	MaxLiftCoefficient = 1.2f * Traits.LiftScale;
	InducedDragFactor = 0.085f * Traits.InducedDragScale;
	if (KiteMesh)
	{
		KiteMesh->SetWorldScale3D(FVector(Scale));
	}
}

void UKiteComponent::SetKiteModel(EKiteModel InModel)
{
	KiteModel = KiteGear::KiteModelFromIndex(static_cast<int32>(InModel));
	SetKiteSize(AreaM2);
}

void UKiteComponent::SetBarEnds(const FVector& LeftEnd, const FVector& RightEnd)
{
	BarLeftEnd = LeftEnd;
	BarRightEnd = RightEnd;
	bHasBarEnds = true;
}

FVector UKiteComponent::GetBarEndWorldPosition(bool bLeft) const
{
	if (bHasBarEnds)
	{
		return bLeft ? BarLeftEnd : BarRightEnd;
	}


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
		return WindComponent->GetWindAtTime(Location, SimTimeSeconds);
	}
	if (const AActor* Owner = GetOwner())
	{
		if (const UWindComponent* FoundWind = Owner->FindComponentByClass<UWindComponent>())
		{
			return FoundWind->GetWindAtTime(Location, SimTimeSeconds);
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
	// Not KiteWindMath::KitePositionInWindow: that clamps azimuth to the downwind half, and a kite
	// flown by a fast rider can sit further round than that.
	const FVector Downwind = GetDownwindDir();
	const FVector Horizontal = Downwind.RotateAngleAxis(InAzimuthDeg, FVector::UpVector);
	const float ElevationRad = FMath::DegreesToRadians(FMath::Clamp(InElevationDeg, 0.0f, 90.0f));
	return Horizontal * FMath::Cos(ElevationRad) + FVector::UpVector * FMath::Sin(ElevationRad);
}

void UKiteComponent::PlaceParked()
{
	const FVector RiderPos = GetRiderPosition();
	const FVector RiderVelocity = GetRiderVelocity();

	KiteDir = DirectionFromAngles(AzimuthDeg, ElevationDeg);
	KiteWorldPosition = RiderPos + KiteDir * LineLengthCm;
	KiteVelocity = RiderVelocity;
	PrevKiteWorldPosition = KiteWorldPosition;

	// A parked kite points its nose out of the window, into the wind blowing across the lines.
	const FVector WindFelt = GetWindAt(KiteWorldPosition) - RiderVelocity;
	const FVector CrossLineWind = WindFelt - FVector::DotProduct(WindFelt, KiteDir) * KiteDir;
	KiteHeading = TangentTowards(KiteDir, -CrossLineWind);

	AirspeedCmS = WindFelt.Size();
	TurnRateRadS = 0.0f;
	AppliedSteer = 0.0f;
	TurnDeg = 0.0f;
	bLooping = false;
	CentredBarSeconds = 0.0f;
	bHasParkClock = false;
	bPlacementPending = false;
	bCrashed = false;
	bLinesTaut = true;
	CrashedSeconds = 0.0f;
}

void UKiteComponent::Crash()
{
	bCrashed = true;
	bLinesTaut = false;
	CrashedSeconds = 0.0f;
	AirspeedCmS = 0.0f;
	TurnDeg = 0.0f;
	LineTensionN = 0.0f;
	LineForce = FVector::ZeroVector;
	KiteVelocity = FVector::ZeroVector;
	KiteWorldPosition.Z = FMath::Min(KiteWorldPosition.Z, CrashHeightCm);
	UpdateAngles();
	KiteHeading = TangentTowards(KiteDir, FVector::UpVector);
	UE_LOG(LogKiteSurf, Log, TEXT("Kite down in the water at azimuth %.0f deg"), AzimuthDeg);
	OnKiteCrashed.Broadcast(KiteWorldPosition);
}

void UKiteComponent::Relaunch()
{
	// Back into the air from where it lies, just above the water, nose-out.
	UpdateAngles();
	ElevationDeg = MinElevationDeg;
	PlaceParked();
	UE_LOG(LogKiteSurf, Log, TEXT("Kite relaunched"));
	OnKiteRelaunched.Broadcast();
}

void UKiteComponent::UpdateAngles()
{
	const FVector Offset = KiteWorldPosition - GetRiderPosition();
	if (!Offset.IsNearlyZero())
	{
		KiteDir = Offset.GetSafeNormal();
	}
	const FVector Downwind = GetDownwindDir();
	const FVector Crosswind = FVector::CrossProduct(FVector::UpVector, Downwind);
	ElevationDeg = FMath::Clamp(FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(KiteDir.Z, -1.0f, 1.0f))), 0.0f, 90.0f);
	AzimuthDeg = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(KiteDir, Crosswind), FVector::DotProduct(KiteDir, Downwind)));
}

void UKiteComponent::GetAeroCoefficients(float AlphaDeg, float& OutLift, float& OutDrag) const
{
	const float Alpha = FMath::UnwindDegrees(AlphaDeg);
	const float AlphaRad = FMath::DegreesToRadians(Alpha);

	// Attached flow: lift rises with angle of attack, rounding off to its maximum at the stall,
	// and drags a little more for it.
	const float AttachedLift = MaxLiftCoefficient * FMath::Sin(FMath::Clamp(Alpha / FMath::Max(StallAngleDeg, 1.0f), -1.0f, 1.0f) * UE_HALF_PI);
	const float AttachedDrag = ParasiteDragCoefficient + InducedDragFactor * FMath::Square(AttachedLift);

	// Separated flow: the canopy is just a plate held across the air, pushed square-on to its
	// surface. Resolved along and across the flow that is less lift and much more drag.
	// A cambered canopy with a fat leading edge still pulls forward a little when stalled, so the
	// push is tilted towards the nose: that is what flies a stalled kite back out to the window edge.
	const float PlateNormalForce = 1.8f * FMath::Sin(AlphaRad);
	const float TiltedRad = AlphaRad - FMath::DegreesToRadians(StalledForwardTiltDeg);
	const float PlateLift = PlateNormalForce * FMath::Cos(TiltedRad);
	const float PlateDrag = ParasiteDragCoefficient + PlateNormalForce * FMath::Sin(TiltedRad);

	// The stall takes hold over a few degrees.
	const float StallBlendDeg = 5.0f;
	const float Separated = FMath::SmoothStep(StallAngleDeg, StallAngleDeg + StallBlendDeg, FMath::Abs(Alpha));
	OutLift = FMath::Lerp(AttachedLift, PlateLift, Separated);
	OutDrag = FMath::Lerp(AttachedDrag, PlateDrag, Separated);
}

float UKiteComponent::ComputeSteering(float DeltaTime, const FVector& RiderVelocity, const FVector& Wind)
{
	const bool bSteering = FMath::Abs(Steer) >= CentredBarThreshold;

	// The bar held towards the side the kite is already on goes straight to the kite, which
	// turns it down and round: a loop. It stays that way until the bar is let go or reversed,
	// so the loop carries on through the rest of the window. Steering towards the other side
	// is a request to fly there, which the assist below carries out over the top.
	const float SteerSide = Steer >= 0.0f ? 1.0f : -1.0f;
	if (!bSteering || (bLooping && SteerSide != LoopSide))
	{
		bLooping = false;
	}
	if (bSteering && !bLooping && (bLoopHeld || GetClockDeg() * SteerSide >= LoopClockDeg))
	{
		bLooping = true;
		LoopSide = SteerSide;
	}
	if (bLooping)
	{
		CentredBarSeconds = 0.0f;
		bHasParkClock = false;
		return Steer;
	}

	CentredBarSeconds += DeltaTime;
	if (CentredBarSeconds >= TurnResetSeconds)
	{
		TurnDeg = 0.0f;
	}

	// Otherwise an assist flies the kite towards a heading. "Out" is into the wind blowing across the lines.
	const FVector WindFelt = Wind - RiderVelocity;
	const FVector CrossLineWind = WindFelt - FVector::DotProduct(WindFelt, KiteDir) * KiteDir;
	const float MinCrossWindCmS = 50.0f; // too little cross wind to say which way is out
	if (CrossLineWind.SizeSquared() < FMath::Square(MinCrossWindCmS))
	{
		return 0.0f;
	}
	const FVector Outward = TangentTowards(KiteDir, -CrossLineWind);

	float OffsetDeg = 0.0f;
	if (bSteering)
	{
		// Bar over: travel round the window that way.
		OffsetDeg = Steer * TravelHeadingDeg;
		bHasParkClock = false;
	}
	else
	{
		// Bar centred: stay at this clock position. Gravity and gusts push the kite along the
		// window edge, so the assist leans the nose against the drift, as a rider's hands would.
		if (!bHasParkClock)
		{
			ParkClockDeg = GetClockDeg();
			bHasParkClock = true;
		}
		const float DriftDeg = FMath::FindDeltaAngleDegrees(GetClockDeg(), ParkClockDeg);
		OffsetDeg = FMath::Clamp(ParkHoldGain * DriftDeg, -ParkHoldMaxDeg, ParkHoldMaxDeg);
	}

	const float OffsetRad = FMath::DegreesToRadians(OffsetDeg);
	FVector TargetHeading = Outward * FMath::Cos(OffsetRad) + FVector::CrossProduct(Outward, KiteDir) * FMath::Sin(OffsetRad);

	// The assist keeps the kite off the water. It looks a moment ahead, because a kite coming
	// down the window at speed needs room to pull out: below the minimum elevation, or about to
	// be, it turns the nose up.
	const float FloorHeightCm = GetRiderPosition().Z + LineLengthCm * FMath::Sin(FMath::DegreesToRadians(MinElevationDeg));
	const float LookAheadSeconds = 1.5f;
	const float HeightSoonCm = KiteWorldPosition.Z + FMath::Min(KiteVelocity.Z - RiderVelocity.Z, 0.0f) * LookAheadSeconds;
	if (KiteWorldPosition.Z < FloorHeightCm)
	{
		TargetHeading = TangentTowards(KiteDir, FVector::UpVector);
	}
	else if (HeightSoonCm < FloorHeightCm)
	{
		TargetHeading = TangentTowards(KiteDir, FVector::UpVector);
	}

	const float ErrorRad = SignedAngleRightRad(KiteHeading, TargetHeading, KiteDir);
	return FMath::Clamp(SteerAssistGain * ErrorRad, -1.0f, 1.0f);
}

float UKiteComponent::StepFlight(float StepSeconds, float SteerInput, const FVector& RiderPos, const FVector& RiderVelocity, const FVector& Wind)
{
	// Everything in here is SI: metres, m/s, newtons. Engine units are converted at the edges.
	const float LineLengthM = LineLengthCm / CmPerM;
	const float InertiaKg = FMath::Max(MassKg + AddedMassKg, 0.1f);

	FVector Offset = KiteWorldPosition - RiderPos;
	const float DistanceCm = Offset.Size();
	const FVector Dir = DistanceCm > KINDA_SMALL_NUMBER ? Offset / DistanceCm : FVector::UpVector;

	const FVector Airflow = (Wind - KiteVelocity) / CmPerM; // air moving past the kite
	const float FlowSpeed = Airflow.Size();
	const float HalfRhoArea = 0.5f * AirDensityKgM3 * AreaM2;
	const FVector Weight(0.0f, 0.0f, -GravityMS2 * MassKg);

	// Held by tight lines, the canopy faces the rider: its normal is along the lines, its nose
	// along KiteHeading, its span across both.
	const FVector Nose = TangentTowards(Dir, KiteHeading);
	const FVector Span = FVector::CrossProduct(Dir, Nose);
	const float ChordFlow = -FVector::DotProduct(Airflow, Nose);   // from nose to tail
	const float NormalFlow = FVector::DotProduct(Airflow, Dir);    // into the underside
	const float SpanFlow = FVector::DotProduct(Airflow, Span);     // sideways
	const float PlaneFlowSpeed = FMath::Sqrt(FMath::Square(ChordFlow) + FMath::Square(NormalFlow));

	const float TrimDeg = FMath::Lerp(TrimSheetedOutDeg, TrimSheetedInDeg, FMath::Clamp(Sheet, 0.0f, 1.0f));
	const float AlphaDeg = PlaneFlowSpeed > 0.05f ? FMath::RadiansToDegrees(FMath::Atan2(NormalFlow, ChordFlow)) + TrimDeg : 0.0f;

	float LiftCoefficient = 0.0f;
	float DragCoefficient = 0.0f;
	GetAeroCoefficients(AlphaDeg, LiftCoefficient, DragCoefficient);
	DragCoefficient += SteeringDragCoefficient * FMath::Square(SteerInput);

	// Lift acts at right angles to the flow in the plane of the nose and the lines; drag along the
	// flow; the side force along the span, against the kite sliding sideways.
	const FVector LiftDir = PlaneFlowSpeed > 0.05f ? (NormalFlow * Nose + ChordFlow * Dir) / PlaneFlowSpeed : Dir;
	const FVector FlyingForce = HalfRhoArea * (
		LiftCoefficient * FMath::Square(PlaneFlowSpeed) * LiftDir
		+ DragCoefficient * FlowSpeed * Airflow
		+ SideForceCoefficient * SpanFlow * FlowSpeed * Span);

	// The lines hold the kite on its arc only if that takes a pull: enough to balance what pushes
	// it away from the rider and to bend its path round the rider.
	const FVector RelativeVelocity = (KiteVelocity - RiderVelocity) / CmPerM;
	const float RadialSpeed = FVector::DotProduct(RelativeVelocity, Dir);
	const float TangentialSpeedSquared = FMath::Max(RelativeVelocity.SizeSquared() - FMath::Square(RadialSpeed), 0.0f);
	const float TensionNeeded = FVector::DotProduct(FlyingForce + Weight, Dir) + InertiaKg * TangentialSpeedSquared / LineLengthM;
	const bool bAtLineLength = DistanceCm >= LineLengthCm - TautSlackCm;
	const bool bTaut = bAtLineLength && TensionNeeded > 0.0f;

	FVector Force;
	float Tension = 0.0f;
	if (bTaut)
	{
		Tension = TensionNeeded;
		Force = FlyingForce + Weight - Tension * Dir;
		AirspeedCmS = PlaneFlowSpeed * CmPerM;
		AngleOfAttackDeg = AlphaDeg;
	}
	else
	{
		// Slack lines. With a little slack the canopy keeps its shape and keeps flying, which is
		// what takes the slack back up: a rider who pops towards the kite does not drop it. With
		// a lot of slack nothing holds it to the wind and it is a sheet in the air: drag and weight.
		const float Collapse = FMath::Clamp((LineLengthCm - TautSlackCm - DistanceCm) / FMath::Max(SlackCollapseCm, 1.0f), 0.0f, 1.0f);
		const FVector SheetForce = HalfRhoArea * SlackDragCoefficient * FlowSpeed * Airflow;
		Force = FMath::Lerp(FlyingForce, SheetForce, Collapse) + Weight;
		TurnRateRadS = 0.0f;
		AirspeedCmS = PlaneFlowSpeed * CmPerM;
		AngleOfAttackDeg = AlphaDeg;

		// A loose canopy is still hanging from its bridle: the rider keeps its nose towards the
		// edge of the window, so when the lines come tight it flies out of the window rather than
		// diving into the water.
		if (StepSeconds > 0.0f)
		{
			const FVector WindFelt = (Wind - RiderVelocity) / CmPerM;
			const FVector CrossLineWind = WindFelt - FVector::DotProduct(WindFelt, Dir) * Dir;
			const FVector OutOfWindow = CrossLineWind.SizeSquared() > 0.25f ? TangentTowards(Dir, -CrossLineWind) : TangentTowards(Dir, FVector::UpVector);
			KiteHeading = TangentTowards(Dir, FMath::Lerp(Nose, OutOfWindow, FMath::Clamp(4.0f * StepSeconds, 0.0f, 1.0f)));
		}
	}
	bLinesTaut = bTaut;

	if (StepSeconds <= 0.0f)
	{
		return Tension;
	}

	// Heading: the bar turns the nose in proportion to the air flowing over the kite; the nose
	// also swings to face the flow, and drops towards the water when the kite is slow.
	if (bTaut)
	{
		const FVector Right = FVector::CrossProduct(Nose, Dir);
		const float SteerRate = SteerInput * FMath::Max(ChordFlow, 0.0f) * CmPerM / FMath::Max(MinTurnRadiusCm, 1.0f);
		const float WeathercockRate = WeathercockGain * SpanFlow;
		const FVector DownTangent = -FVector::UpVector + FVector::DotProduct(FVector::UpVector, Dir) * Dir;
		const float MinSpeedForGravityTurn = 3.0f;
		const float GravityRate = GravityTurnGain * GravityMS2 * FVector::DotProduct(DownTangent, Right) / FMath::Max(PlaneFlowSpeed, MinSpeedForGravityTurn);

		// The kite has some inertia in yaw: it winds into a turn rather than snapping round.
		TurnRateRadS = FMath::FInterpTo(TurnRateRadS, SteerRate + WeathercockRate + GravityRate, StepSeconds, TurnResponse);
		const float TurnRad = TurnRateRadS * StepSeconds;
		KiteHeading = Nose * FMath::Cos(TurnRad) + Right * FMath::Sin(TurnRad);
		if (bLooping)
		{
			TurnDeg += FMath::RadiansToDegrees(TurnRad);
		}
	}

	// Integrate, then let the lines catch the kite at their full length.
	KiteVelocity += Force / InertiaKg * StepSeconds * CmPerM;
	KiteWorldPosition += KiteVelocity * StepSeconds;

	Offset = KiteWorldPosition - RiderPos;
	const float NewDistanceCm = Offset.Size();
	if (bTaut && NewDistanceCm > KINDA_SMALL_NUMBER)
	{
		// Tight lines are a rod: the kite stays at line length and moves round the rider, not
		// towards or away from them.
		const FVector NewDir = Offset / NewDistanceCm;
		KiteWorldPosition = RiderPos + NewDir * LineLengthCm;
		KiteVelocity -= FVector::DotProduct(KiteVelocity - RiderVelocity, NewDir) * NewDir;
		KiteDir = NewDir;
	}
	else if (NewDistanceCm > LineLengthCm)
	{
		// Slack lines snatch tight when the kite reaches their full length.
		const FVector NewDir = Offset / NewDistanceCm;
		KiteWorldPosition = RiderPos + NewDir * LineLengthCm;
		const float OutwardSpeed = FVector::DotProduct(KiteVelocity - RiderVelocity, NewDir);
		if (OutwardSpeed > 0.0f)
		{
			KiteVelocity -= OutwardSpeed * NewDir;
		}
		KiteDir = NewDir;
	}
	else if (NewDistanceCm > KINDA_SMALL_NUMBER)
	{
		KiteDir = Offset / NewDistanceCm;
	}
	KiteHeading = TangentTowards(KiteDir, KiteHeading);

	return Tension;
}

void UKiteComponent::UpdateKite(float DeltaTime)
{
	if (DeltaTime <= 0.0f)
	{
		StepKite(0.0f);
		return;
	}

	// Fixed small steps: the kite's response is fast compared with a frame.
	const int32 NumSteps = FMath::Clamp(FMath::CeilToInt(DeltaTime / FMath::Max(MaxStepSeconds, KINDA_SMALL_NUMBER)), 1, FMath::Max(MaxStepsPerUpdate, 1));
	const float StepSeconds = DeltaTime / NumSteps;
	for (int32 Step = 0; Step < NumSteps; ++Step)
	{
		StepKite(StepSeconds);
	}
}

void UKiteComponent::StepKite(float StepSeconds)
{
	if (bPlacementPending)
	{
		PlaceParked();
	}
	SimTimeSeconds += FMath::Max(StepSeconds, 0.0f);
	PrevKiteWorldPosition = KiteWorldPosition;

	const FVector RiderPos = GetRiderPosition();
	const FVector RiderVelocity = GetRiderVelocity();

	if (bCrashed)
	{
		// On the water: slack lines, towed along if the rider rides away from it, until it relaunches.
		CrashedSeconds += StepSeconds;
		FVector Offset = KiteWorldPosition - RiderPos;
		if (Offset.Size() > LineLengthCm)
		{
			KiteWorldPosition = RiderPos + Offset.GetSafeNormal() * LineLengthCm;
		}
		KiteWorldPosition.Z = FMath::Min(KiteWorldPosition.Z, CrashHeightCm);
		KiteVelocity = FVector::ZeroVector;
		LineTensionN = 0.0f;
		LineForce = FVector::ZeroVector;
		AppliedSteer = 0.0f;
		UpdateAngles();

		const float MinSecondsBeforeSteeredRelaunch = 1.0f;
		const bool bSteeredUp = CrashedSeconds >= MinSecondsBeforeSteeredRelaunch && FMath::Abs(Steer) >= CentredBarThreshold;
		// It will not come off the water without enough wind to fly in.
		const bool bEnoughWind = (GetWindAt(KiteWorldPosition) - RiderVelocity).SizeSquared() >= FMath::Square(MinRelaunchWindCmS);
		if (bEnoughWind && (CrashedSeconds >= RelaunchDelaySeconds || bSteeredUp))
		{
			Relaunch();
		}
		return;
	}

	const FVector Wind = GetWindAt(KiteWorldPosition);
	const float SteerInput = ComputeSteering(StepSeconds, RiderVelocity, Wind);
	AppliedSteer = SteerInput;

	const float Tension = StepFlight(StepSeconds, SteerInput, RiderPos, RiderVelocity, Wind);
	if (StepSeconds > 0.0f && KiteWorldPosition.Z <= CrashHeightCm)
	{
		Crash();
		return;
	}

	UpdateAngles();

	// The rider feels the pull along the lines.
	LineTensionN = FMath::Min(Tension, MaxLineTensionN);
	LineForce = KiteDir * KiteUnits::NToUnrealForce(LineTensionN);

#if !UE_BUILD_SHIPPING
	if (bDrawDebug && GetWorld())
	{
		DrawDebugLine(GetWorld(), RiderPos, KiteWorldPosition, bLinesTaut ? FColor::Yellow : FColor::Red, false, -1.0f, 0, 2.0f);
		DrawDebugLine(GetWorld(), KiteWorldPosition, KiteWorldPosition + KiteHeading * 300.0f, FColor::Red, false, -1.0f, 0, 2.0f);

		const FString DebugText = FString::Printf(TEXT("Tension: %.0f N%s\nAirspeed: %.1f m/s\nAngle of attack: %.1f\nAz: %.1f, El: %.1f\nSheet: %.2f"),
			LineTensionN, bLinesTaut ? TEXT("") : TEXT(" (slack)"), AirspeedCmS / CmPerM, AngleOfAttackDeg, AzimuthDeg, ElevationDeg, Sheet);
		DrawDebugString(GetWorld(), KiteWorldPosition + FVector(0.0f, 0.0f, 50.0f), DebugText, nullptr, FColor::White, 0.0f, true);
	}
#endif
}
