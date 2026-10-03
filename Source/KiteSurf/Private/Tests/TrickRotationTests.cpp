#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "BoardMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "KiteComponent.h"
#include "KiteRiderPawn.h"
#include "KiteSurf.h"
#include "KiteSurfGameMode.h"
#include "KiteSurfUnits.h"
#include "RiderRig.h"
#include "WindComponent.h"
#include "Tricks/LandingEvaluator.h"
#include "Tricks/RiderAttitudeComponent.h"
#include "Tricks/RiderAxes.h"

#if WITH_DEV_AUTOMATION_TESTS

// Rider rotation live (T1.2 PR E, T1.5 wiring): the attitude is stepped by the pawn before the
// board, the board flies and lands with it, the drawn board and rider follow it, and the landing is
// graded by LandingEvaluator. The ride tests fly the phase 2 timed jump (30 kn, the recommended kite,
// the jump button held through the send and let go to pop) with the rotation input scripted.

namespace TrickRotationTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float DefaultFrameSeconds = 1.0f / 60.0f;

	/** Inversion count as in docs/tricks.md section 6.6: body Up below -0.3 after being above +0.3. */
	struct FInversionTally
	{
		bool bArmed = true;
		int32 Count = 0;
		float FirstInvertedAt = -1.0f;
		float BackUprightAt = -1.0f;

		void Add(double UpDotWorldUp, float Time)
		{
			if (bArmed && UpDotWorldUp < -0.3)
			{
				++Count;
				bArmed = false;
				if (FirstInvertedAt < 0.0f) { FirstInvertedAt = Time; }
			}
			else if (!bArmed && UpDotWorldUp > 0.3)
			{
				bArmed = true;
				if (BackUprightAt < 0.0f) { BackUprightAt = Time; }
			}
		}
	};

	/**
	 * A rider on the water in steady wind along +X, started on a beam reach as the game mode does,
	 * stepped one frame at a time, with the camera boom's lag off and the boom ticked after the pawn
	 * (the bare world does not tick it).
	 */
	struct FTrickRide
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UKiteComponent* Kite = nullptr;
		UBoardMovementComponent* Board = nullptr;
		URiderAttitudeComponent* Attitude = nullptr;
		USpringArmComponent* Boom = nullptr;
		UCameraComponent* Camera = nullptr;
		float FrameSeconds = DefaultFrameSeconds;

		explicit FTrickRide(float WindKnots = 30.0f, float InFrameSeconds = DefaultFrameSeconds)
			: FrameSeconds(InFrameSeconds)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
			if (!Pawn)
			{
				return;
			}
			Kite = Pawn->GetKite();
			Board = Pawn->GetBoardMovement();
			Attitude = Pawn->GetRiderAttitude();
			Boom = Pawn->FindComponentByClass<USpringArmComponent>();
			Camera = Pawn->FindComponentByClass<UCameraComponent>();
			if (!Kite || !Board)
			{
				return;
			}
			if (UWindComponent* Wind = Pawn->GetWind())
			{
				Wind->BaseWind = FVector(KiteUnits::KnotsToCmS(WindKnots), 0.0f, 0.0f);
				Wind->GustStrength = 0.0f;
				Wind->DirectionDriftDeg = 0.0f;
			}
			AKiteSurfGameMode::InitializeRide(Pawn, KiteUnits::KnotsToCmS(12.0f), 1.0f);
			Kite->bParkHoldAssist = true;
			Kite->SetKiteModel(EKiteModel::Loop);
			Kite->SetKiteSize(UKiteComponent::RecommendKiteSizeM2(WindKnots));
			if (Boom)
			{
				Boom->bEnableCameraLag = false;
				Boom->bEnableCameraRotationLag = false;
			}
		}

		~FTrickRide()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		bool IsValid() const { return Pawn && Kite && Board && Attitude && Boom && Camera; }

		void Frame()
		{
			Pawn->Tick(FrameSeconds);
			Boom->TickComponent(FrameSeconds, LEVELTICK_All, nullptr);
		}

		bool HasReached(float SimSeconds) const
		{
			return Pawn->GetSimTimeSeconds() + 0.5f * Pawn->SimStepSeconds >= SimSeconds;
		}

		void SimulateUntil(float SimSeconds)
		{
			while (!HasReached(SimSeconds))
			{
				Frame();
			}
		}

		bool IsAirborne() const { return Board->GetBoardState() == EBoardState::Airborne; }
	};

	/** The feet are in the straps of the drawn board: the ankles either side of its middle along its length, AnkleHeightCm above the deck. Returns the worst miss (cm). */
	float FeetOffVisualStrapsCm(const AKiteRiderPawn* Pawn)
	{
		const FRiderRigPose& Pose = Pawn->GetRiderRigPose();
		const FTransform& Board = Pawn->GetBoardVisual()->GetComponentTransform();
		const FVector Centre = Board.GetLocation() + Board.GetUnitAxis(EAxis::Z) * RiderRig::AnkleHeightCm;
		const FVector Along = Board.GetUnitAxis(EAxis::X) * RiderRig::StrapHalfSpacingCm;
		float Worst = 0.0f;
		for (const FVector& Ankle : { Pose.Legs[0].End, Pose.Legs[1].End })
		{
			Worst = FMath::Max(Worst, static_cast<float>(FMath::Min(FVector::Dist(Ankle, Centre + Along), FVector::Dist(Ankle, Centre - Along))));
		}
		return Worst;
	}

	struct FTrickJumpResult
	{
		bool bValid = false;
		bool bTookOff = false;
		bool bLanded = false;
		bool bCrashedAfter = false;
		bool bNaN = false;
		float AirSeconds = 0.0f;
		float PeakCm = 0.0f;
		int32 Inversions = 0;
		float FirstInvertedAt = -1.0f;
		float BackUprightAt = -1.0f;
		/** Rotation about world up over the airtime (deg), from the attitude's angular velocity. */
		float SpinAboutUpDeg = 0.0f;
		float MaxTiltDeg = 0.0f;
		bool bAssistActed = false;
		/** Chest movement towards the tail (-T) in the first 0.15 s of the air. */
		float ChestToTail = 0.0f;
		bool bAttitudeLiveInAir = true;
		FLandingVerdict Verdict;
		FLandingInputs LandingInputs;
		float LandingAirtime = 0.0f;
		// Visual checks, in the air from 0.3 s after the take-off on.
		float MaxCameraRollDeg = 0.0f;
		float MaxPivotErrorCm = 0.0f;
		float MaxVisualOffAttitudeDeg = 0.0f;
		float MaxRootTiltDeg = 0.0f;
		float WorstFeetOffStrapsCm = 0.0f;
		float WorstFeetOffStrapsInvertedCm = -1.0f;
		/** Largest frame-to-frame jump of the pelvis relative to the root (cm), over the whole run. */
		float MaxPelvisJumpCm = 0.0f;
		float PelvisJumpAtTakeoffCm = 0.0f;
		float PelvisJumpAtLandingCm = 0.0f;
		/** Half a second after the landing, the drawn board is back on the root. */
		bool bVisualOnRootAfter = false;
		bool bOverrideAfter = true;
		/** How far the pre-wind had built when the rider let go to pop. */
		float PreWindAtRelease = 0.0f;
		/** The flight's change of heading over the airtime (deg), from the horizontal velocity at take-off and at touchdown. */
		float TravelTurnDeg = 0.0f;
	};

	/**
	 * The phase 2 timed jump with a scripted pre-wind: 8 s of riding, then the bar hard over, the
	 * weight on the tail and the jump button held with the pre-wind stick, let go to pop 0.66 s after
	 * the bar reaches the kite (rounded to 1/30 s, so every frame rate pops in the same step), the bar
	 * centred in the air, and a crouch for the landing from the apex. Every input goes in at the same
	 * simulation time at any frame length, except the crouch, which only changes the absorb distance.
	 */
	FTrickJumpResult RunTrickJump(const FVector2D& PreWind, float FrameSeconds = DefaultFrameSeconds)
	{
		FTrickJumpResult R;
		FTrickRide Ride(30.0f, FrameSeconds);
		if (!Ride.IsValid())
		{
			return R;
		}
		R.bValid = true;
		AKiteRiderPawn* Pawn = Ride.Pawn;
		const float SendAt = 8.0f;
		Ride.SimulateUntil(SendAt);
		Pawn->SteerKite(-1.0f);
		Ride.Board->SetWeightShift(-1.0f);
		Pawn->SetLoadHeld(true);
		Pawn->SetPreWind(PreWind);
		R.PreWindAtRelease = -1.0f;
		const float ReleaseAt = SendAt + FMath::RoundToFloat((0.66f + Ride.Kite->GetSteeringDeadTimeSeconds()) * 30.0f) / 30.0f;
		while (!Ride.HasReached(ReleaseAt) && !Ride.IsAirborne())
		{
			Ride.Frame();
		}
		R.PreWindAtRelease = Pawn->GetPreWindAmount();
		Ride.Pawn->SheetKite(1.0f);
		Pawn->ReleaseLoadAndPop();
		Ride.Board->SetWeightShift(0.0f);
		Pawn->SteerKite(0.0f);
		Pawn->SetPreWind(FVector2D::ZeroVector);

		FInversionTally Tally;
		FVector Front0 = FVector::ZeroVector;
		FVector Travel0 = FVector::ZeroVector;
		bool bChestMeasured = false;
		FVector LastPelvisRel = Pawn->GetRiderRigPose().Pelvis - Pawn->GetActorLocation();
		bool bWasAir = false;
		float LastAirTravelYawDeg = 0.0f;
		const float EndAt = ReleaseAt + 20.0f;
		while (!Ride.HasReached(EndAt))
		{
			Ride.Frame();
			const bool bAir = Ride.IsAirborne();
			const FVector PelvisRel = Pawn->GetRiderRigPose().Pelvis - Pawn->GetActorLocation();
			const float PelvisJump = static_cast<float>(FVector::Dist(PelvisRel, LastPelvisRel));
			LastPelvisRel = PelvisRel;
			R.MaxPelvisJumpCm = FMath::Max(R.MaxPelvisJumpCm, PelvisJump);
			R.bNaN |= Pawn->GetActorLocation().ContainsNaN() || Ride.Attitude->GetBodyQuat().ContainsNaN();
			if (bAir)
			{
				if (!bWasAir)
				{
					R.bTookOff = true;
					R.PelvisJumpAtTakeoffCm = PelvisJump;
					Front0 = Ride.Attitude->GetBodyQuat().GetAxisX();
					Travel0 = FVector(Ride.Board->Velocity.X, Ride.Board->Velocity.Y, 0.0f).GetSafeNormal();
				}
				bWasAir = true;
				R.AirSeconds += FrameSeconds;
				R.PeakCm = FMath::Max(R.PeakCm, Ride.Board->GetCurrentJumpHeight());
				R.bAttitudeLiveInAir &= Ride.Attitude->IsSimulating();
				LastAirTravelYawDeg = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Ride.Board->Velocity.Y, Ride.Board->Velocity.X)));
				const FQuat Body = Ride.Attitude->GetBodyQuat();
				const double UpZ = Body.GetAxisZ().Z;
				Tally.Add(UpZ, R.AirSeconds);
				R.MaxTiltDeg = FMath::Max(R.MaxTiltDeg, static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(UpZ, -1.0, 1.0)))));
				R.SpinAboutUpDeg += static_cast<float>(FMath::RadiansToDegrees(Ride.Attitude->GetAngularVelocity().Z) * FrameSeconds);
				R.bAssistActed |= Ride.Attitude->GetLastStepDebug().bAssistActive;
				if (!bChestMeasured && R.AirSeconds >= 0.15f)
				{
					R.ChestToTail = static_cast<float>((Body.GetAxisX() - Front0) | -Travel0);
					bChestMeasured = true;
				}
				if (Ride.Board->Velocity.Z < 0.0f)
				{
					Pawn->SetLoadHeld(true); // coming down: crouch for the landing
				}
				if (R.AirSeconds >= 0.3f)
				{
					R.MaxCameraRollDeg = FMath::Max(R.MaxCameraRollDeg, FMath::Abs(static_cast<float>(Ride.Camera->GetComponentRotation().Roll)));
					R.MaxPivotErrorCm = FMath::Max(R.MaxPivotErrorCm, FMath::Abs(static_cast<float>(Ride.Boom->GetComponentLocation().Z - Pawn->GetActorLocation().Z) - Pawn->CameraPivotHeightCm));
					R.MaxVisualOffAttitudeDeg = FMath::Max(R.MaxVisualOffAttitudeDeg,
						static_cast<float>(FMath::RadiansToDegrees(Pawn->GetBoardVisual()->GetComponentQuat().AngularDistance(Ride.Attitude->GetBoardQuat()))));
					const FRotator Root = Pawn->GetActorRotation();
					R.MaxRootTiltDeg = FMath::Max(R.MaxRootTiltDeg, FMath::Max(FMath::Abs(static_cast<float>(Root.Pitch)), FMath::Abs(static_cast<float>(Root.Roll))));
					const float FeetOff = FeetOffVisualStrapsCm(Pawn);
					R.WorstFeetOffStrapsCm = FMath::Max(R.WorstFeetOffStrapsCm, FeetOff);
					if (UpZ < -0.3)
					{
						R.WorstFeetOffStrapsInvertedCm = FMath::Max(R.WorstFeetOffStrapsInvertedCm, FeetOff);
					}
				}
			}
			else if (bWasAir)
			{
				R.bLanded = true;
				R.PelvisJumpAtLandingCm = PelvisJump;
				R.Verdict = Ride.Board->GetLastLandingVerdict();
				R.LandingInputs = Ride.Board->GetLastLandingInputs();
				R.LandingAirtime = Ride.Board->GetLastJumpAirtime();
				R.TravelTurnDeg = FMath::FindDeltaAngleDegrees(static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Travel0.Y, Travel0.X))), LastAirTravelYawDeg);
				break;
			}
		}
		R.Inversions = Tally.Count;
		R.FirstInvertedAt = Tally.FirstInvertedAt;
		R.BackUprightAt = Tally.BackUprightAt;
		Pawn->SetLoadHeld(false); // up out of the crouch (no pop on the water)
		for (float T = 0.0f; T < 1.0f; T += FrameSeconds)
		{
			Ride.Frame();
			const FVector PelvisRel = Pawn->GetRiderRigPose().Pelvis - Pawn->GetActorLocation();
			R.MaxPelvisJumpCm = FMath::Max(R.MaxPelvisJumpCm, static_cast<float>(FVector::Dist(PelvisRel, LastPelvisRel)));
			LastPelvisRel = PelvisRel;
		}
		R.bCrashedAfter = Ride.Board->IsCrashing();
		UStaticMeshComponent* Visual = Pawn->GetBoardVisual();
		const USceneComponent* Root = Pawn->GetRootComponent();
		R.bVisualOnRootAfter = Visual->GetComponentLocation().Equals(Root->GetComponentLocation(), 0.01f)
			&& Visual->GetComponentQuat().Equals(Root->GetComponentQuat(), 1e-4f);
		R.bOverrideAfter = Pawn->HasBoardVisualOverride();
		return R;
	}

	FString Describe(const FTrickJumpResult& R)
	{
		const UEnum* GradeEnum = StaticEnum<ELandingGrade>();
		const UEnum* CauseEnum = StaticEnum<ELandingCause>();
		return FString::Printf(TEXT("pre-wind %.2f, air %.2f s, peak %.1f m, %d inversion(s) (first %.2f s, upright again %.2f s), spin about up %.0f deg (the flight turned %.0f), max tilt %.0f deg, assist %d, chest to tail %.3f; landed %s %s: tilt %.1f deg, yaw %.1f deg, rider up %.2f, %.2f g"),
			R.PreWindAtRelease, R.AirSeconds, R.PeakCm / 100.0f, R.Inversions, R.FirstInvertedAt, R.BackUprightAt, R.SpinAboutUpDeg, R.TravelTurnDeg, R.MaxTiltDeg, R.bAssistActed, R.ChestToTail,
			GradeEnum ? *GradeEnum->GetNameStringByValue(static_cast<int64>(R.Verdict.Grade)) : TEXT("?"),
			CauseEnum ? *CauseEnum->GetNameStringByValue(static_cast<int64>(R.Verdict.Cause)) : TEXT("?"),
			R.LandingInputs.TiltDeg, R.LandingInputs.YawOffVelocityDeg, R.LandingInputs.BodyUpDot, R.LandingInputs.LandingG);
	}

	/** A pawn with the board just above flat water, airborne at this velocity, no wind; for landings judged on the board alone. */
	struct FBoardLanding
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UBoardMovementComponent* Board = nullptr;

		FBoardLanding(float RootYawDeg, const FVector& Velocity)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
			Board = Pawn ? Pawn->GetBoardMovement() : nullptr;
			if (!Board)
			{
				return;
			}
			Pawn->SetActorRotation(FRotator(0.0f, RootYawDeg, 0.0f));
			Pawn->SetActorLocation(FVector(0.0f, 0.0f, 8.0f));
			Board->Velocity = Velocity;
			Board->SetBoardState(EBoardState::Airborne);
			Board->SetCurrentJumpAirtime(0.5f);
		}

		~FBoardLanding()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		bool IsValid() const { return Pawn && Board; }

		/** One frame of the board alone, as BoardMovementTests drive it. */
		void Tick(float Seconds = 1.0f / 30.0f) { Board->TickComponent(Seconds, LEVELTICK_All, nullptr); }
	};
}


// A scripted back-roll pre-wind on the timed jump: one inversion, the chest to the tail first, upright
// again before the water, and a landing the assist saves.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickBackRollFromPreWindOnRide, "KiteSurf.Trick.BackRollFromPreWindOnRide", TrickRotationTest::Flags)

bool FKiteSurfTrickBackRollFromPreWindOnRide::RunTest(const FString& Parameters)
{
	using namespace TrickRotationTest;
	const FTrickJumpResult R = RunTrickJump(FVector2D(1.0f, 0.0f));
	AddInfo(FString::Printf(TEXT("Back roll on the timed jump: %s"), *Describe(R)));
	TestTrue(TEXT("Ride fixture created"), R.bValid);
	TestTrue(TEXT("The rider took off and landed"), R.bTookOff && R.bLanded);
	TestTrue(TEXT("The attitude was live all through the air"), R.bAttitudeLiveInAir);
	TestTrue(FString::Printf(TEXT("The pre-wind was fully wound up by the pop (%.2f)"), R.PreWindAtRelease), R.PreWindAtRelease > 0.99f);
	TestFalse(TEXT("Nothing went NaN"), R.bNaN);
	if (!TestTrue(FString::Printf(TEXT("Precondition: at least 3 s in the air (%.2f s)"), R.AirSeconds), R.AirSeconds >= 3.0f))
	{
		return false;
	}
	TestEqual(TEXT("Exactly one inversion"), R.Inversions, 1);
	TestTrue(TEXT("Inverted inside the airtime"), R.FirstInvertedAt > 0.0f && R.FirstInvertedAt < R.AirSeconds);
	TestTrue(TEXT("Upright again before the water"), R.BackUprightAt > R.FirstInvertedAt && R.BackUprightAt < R.AirSeconds);
	TestTrue(TEXT("A back roll turns the chest towards the tail first"), R.ChestToTail > 0.0f);
	TestTrue(TEXT("The landing assist acted on the way in"), R.bAssistActed);
	TestTrue(TEXT("Landed, not crashed"), R.Verdict.Grade != ELandingGrade::Crash && !R.bCrashedAfter);
	return true;
}

// The same jump with no rotation input: the rider hangs from the lines, the board follows the flight
// and nothing inverts.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickNoInputNoRotationOnRide, "KiteSurf.Trick.NoInputNoRotationOnRide", TrickRotationTest::Flags)

bool FKiteSurfTrickNoInputNoRotationOnRide::RunTest(const FString& Parameters)
{
	using namespace TrickRotationTest;
	const FTrickJumpResult R = RunTrickJump(FVector2D::ZeroVector);
	AddInfo(FString::Printf(TEXT("Timed jump, no rotation input: %s"), *Describe(R)));
	TestTrue(TEXT("Ride fixture created"), R.bValid);
	TestTrue(TEXT("The rider took off and landed"), R.bTookOff && R.bLanded);
	TestTrue(TEXT("The attitude was live all through the air"), R.bAttitudeLiveInAir);
	TestEqual(TEXT("No inversions"), R.Inversions, 0);
	// The travel align keeps the board along the flight, and the kite turns the flight downwind as the
	// rider hangs under it: the rotation that counts is against the flight's own turn.
	TestTrue(FString::Printf(TEXT("Rotation about world up within 30 deg of the flight's turn (%.1f against %.1f)"), R.SpinAboutUpDeg, R.TravelTurnDeg),
		FMath::Abs(R.SpinAboutUpDeg - R.TravelTurnDeg) < 30.0f);
	TestTrue(FString::Printf(TEXT("Body tilt under 50 deg throughout (%.1f)"), R.MaxTiltDeg), R.MaxTiltDeg < 50.0f);
	TestTrue(TEXT("Landed, not crashed"), R.Verdict.Grade != ELandingGrade::Crash && !R.bCrashedAfter);
	TestTrue(FString::Printf(TEXT("Landed straight: tilt under 15 deg (%.1f) and yaw under 20 deg (%.1f)"), R.LandingInputs.TiltDeg, R.LandingInputs.YawOffVelocityDeg),
		R.LandingInputs.TiltDeg < 15.0f && R.LandingInputs.YawOffVelocityDeg < 20.0f);
	return true;
}

// A rider coming down tilted or upside down crashes, with the cause the evaluator names. No wind and
// no line torque, the assist off: the rotation is whatever the test put there.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickUnderRotatedRollCrashesOnPawn, "KiteSurf.Trick.UnderRotatedRollCrashesOnPawn", TrickRotationTest::Flags)

bool FKiteSurfTrickUnderRotatedRollCrashesOnPawn::RunTest(const FString& Parameters)
{
	using namespace TrickRotationTest;
	struct FCase
	{
		const TCHAR* Name;
		float ShortOfUprightDeg;
		float SpinOnRadS;
		ELandingCause Expected;
	};
	// 80 deg short of upright about the body's front, still turning slowly towards it: the board meets
	// the water on its rail (tilt past the Sketchy 50), under-rotated. 180 deg: upside down.
	const FCase Cases[] = {
		{ TEXT("80 deg short, still rolling"), 80.0f, 0.3f, ELandingCause::UnderRotated },
		{ TEXT("upside down"), 180.0f, 0.0f, ELandingCause::Inverted },
	};
	for (const FCase& Case : Cases)
	{
		FTrickRide Ride(0.0f);
		TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
		if (!Ride.IsValid())
		{
			return false;
		}
		AKiteRiderPawn* Pawn = Ride.Pawn;
		Ride.Attitude->AssistStrength = 0.0f;
		Ride.Attitude->LineTorqueScale = 0.0f;
		Ride.Attitude->TravelAlignNaturalFreqHz = 0.0f;
		Ride.Frame(); // on the water: the attitude takes the riding pose
		TestEqual(TEXT("Pop accepted"), Ride.Board->Jump(), EJumpRejectReason::None);
		const float Yaw = static_cast<float>(Pawn->GetActorRotation().Yaw);
		// Low enough that the landing is not too hard for the legs: the cause is the attitude's.
		Pawn->SetActorLocation(FVector(Pawn->GetActorLocation().X, Pawn->GetActorLocation().Y, 150.0f));
		Ride.Board->Velocity = FRotator(0.0f, Yaw, 0.0f).Vector() * 800.0f + FVector(0.0f, 0.0f, -300.0f);
		const FQuat Body0 = Ride.Attitude->GetBodyQuat();
		const FVector Axis = Body0.GetAxisX();
		const FQuat Body = FQuat(Axis, FMath::DegreesToRadians(-Case.ShortOfUprightDeg)) * Body0;
		Ride.Attitude->SetState(Body, Axis * (Case.SpinOnRadS * Ride.Attitude->InertiaStretchedKgM2.X));

		bool bLanded = false;
		for (int32 Frame = 0; Frame < 240 && !bLanded; ++Frame)
		{
			Ride.Frame();
			bLanded = !Ride.IsAirborne();
		}
		const FLandingVerdict Verdict = Ride.Board->GetLastLandingVerdict();
		const FLandingInputs& In = Ride.Board->GetLastLandingInputs();
		const UEnum* CauseEnum = StaticEnum<ELandingCause>();
		AddInfo(FString::Printf(TEXT("%s: landed %d, tilt %.1f deg, yaw %.1f deg, rider up %.2f, error along spin %.2f, cause %s"), Case.Name, bLanded,
			In.TiltDeg, In.YawOffVelocityDeg, In.BodyUpDot, In.ErrorAlongSpin, CauseEnum ? *CauseEnum->GetNameStringByValue(static_cast<int64>(Verdict.Cause)) : TEXT("?")));
		TestTrue(FString::Printf(TEXT("%s: came down to the water"), Case.Name), bLanded);
		TestEqual(FString::Printf(TEXT("%s: a crash"), Case.Name), Verdict.Grade, ELandingGrade::Crash);
		TestEqual(FString::Printf(TEXT("%s: cause"), Case.Name), Verdict.Cause, Case.Expected);
		TestTrue(FString::Printf(TEXT("%s: the board is crashing"), Case.Name), Ride.Board->IsCrashing());
		Ride.Frame();
		TestFalse(FString::Printf(TEXT("%s: the crash ends the rotation"), Case.Name), Ride.Attitude->IsSimulating());
		TestFalse(FString::Printf(TEXT("%s: and puts the drawn board back on the root"), Case.Name), Pawn->HasBoardVisualOverride());
	}
	return true;
}

// A switch landing, the tail first, is a valid landing: the evaluator folds the yaw to either end.
// Without the attitude (the root turned 180 deg) and with it (the attitude's board turned 180 deg).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickSwitchLandingIsCleanOnBoard, "KiteSurf.Trick.SwitchLandingIsCleanOnBoard", TrickRotationTest::Flags)

bool FKiteSurfTrickSwitchLandingIsCleanOnBoard::RunTest(const FString& Parameters)
{
	using namespace TrickRotationTest;
	for (const bool bWithAttitude : { false, true })
	{
		const TCHAR* Which = bWithAttitude ? TEXT("with the attitude") : TEXT("board alone");
		FBoardLanding Landing(bWithAttitude ? 0.0f : 180.0f, FVector(600.0f, 0.0f, -50.0f));
		TestTrue(TEXT("Fixture created"), Landing.IsValid());
		if (!Landing.IsValid())
		{
			return false;
		}
		if (bWithAttitude)
		{
			const FQuat Board = FRotator(0.0f, 180.0f, 0.0f).Quaternion();
			Landing.Board->SetAirAttitude(Board, true, FRotator(0.0f, 90.0f, 0.0f).Quaternion());
		}
		Landing.Tick();
		const FLandingVerdict Verdict = Landing.Board->GetLastLandingVerdict();
		AddInfo(FString::Printf(TEXT("Switch landing (%s): grade %d, yaw %.1f deg, tilt %.1f deg"), Which, static_cast<int32>(Verdict.Grade),
			Landing.Board->GetLastLandingInputs().YawOffVelocityDeg, Landing.Board->GetLastLandingInputs().TiltDeg));
		TestEqual(FString::Printf(TEXT("%s: one landing"), Which), Landing.Board->GetLandingCount(), 1);
		TestFalse(FString::Printf(TEXT("%s: not crashing"), Which), Landing.Board->IsCrashing());
		TestTrue(FString::Printf(TEXT("%s: stomped or clean"), Which), Verdict.Grade == ELandingGrade::Stomped || Verdict.Grade == ELandingGrade::Clean);
		TestTrue(FString::Printf(TEXT("%s: the root heads tail first"), Which), Landing.Board->GetForwardSpeed() < 0.0f);
		for (int32 Frame = 0; Frame < 18; ++Frame)
		{
			Landing.Tick();
		}
		TestTrue(FString::Printf(TEXT("%s: 0.6 s later the twin-tip has swapped ends and rides on (%.0f cm/s)"), Which, Landing.Board->GetForwardSpeed()),
			Landing.Board->GetForwardSpeed() > 0.0f);
	}
	return true;
}

// The board's landing is the evaluator's: a yaw the old single 30 deg test crashed is now sketchy and
// slower; the attitude's tilt and the rider's up count; the verdict is kept.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickBoardLandingUsesEvaluator, "KiteSurf.Trick.BoardLandingUsesEvaluator", TrickRotationTest::Flags)

bool FKiteSurfTrickBoardLandingUsesEvaluator::RunTest(const FString& Parameters)
{
	using namespace TrickRotationTest;
	const float Speed = 600.0f;
	// Yaw 50 deg off the course, board alone: Sketchy (it was a crash past 30 deg), 0.6 of the speed kept.
	{
		FBoardLanding Landing(50.0f, FVector(Speed, 0.0f, -50.0f));
		if (!TestTrue(TEXT("Fixture created"), Landing.IsValid())) { return false; }
		Landing.Tick();
		const FLandingVerdict Verdict = Landing.Board->GetLastLandingVerdict();
		TestEqual(TEXT("Yaw 50: sketchy"), Verdict.Grade, ELandingGrade::Sketchy);
		TestFalse(TEXT("Yaw 50: no crash"), Landing.Board->IsCrashing());
		TestTrue(TEXT("Yaw 50: still counted as landed (WasLastLandingClean)"), Landing.Board->WasLastLandingClean());
		TestNearlyEqual(TEXT("Yaw 50: the sketchy share of the speed is kept"), Verdict.SpeedRetention, Landing.Board->LandingThresholds.SpeedRetentionSketchy, 1e-4f);
	}
	// Yaw 80: past the Sketchy 75, a crash, sideways.
	{
		FBoardLanding Landing(80.0f, FVector(Speed, 0.0f, -50.0f));
		if (!TestTrue(TEXT("Fixture created"), Landing.IsValid())) { return false; }
		Landing.Tick();
		TestEqual(TEXT("Yaw 80: crash"), Landing.Board->GetLastLandingVerdict().Grade, ELandingGrade::Crash);
		TestEqual(TEXT("Yaw 80: sideways"), Landing.Board->GetLastLandingVerdict().Cause, ELandingCause::Sideways);
		TestTrue(TEXT("Yaw 80: crashing"), Landing.Board->IsCrashing());
	}
	// The attitude's board on its rail (60 deg about its length), rider upright: past the Sketchy tilt, a crash.
	{
		FBoardLanding Landing(0.0f, FVector(Speed, 0.0f, -50.0f));
		if (!TestTrue(TEXT("Fixture created"), Landing.IsValid())) { return false; }
		const FQuat Board = FQuat(FVector::ForwardVector, FMath::DegreesToRadians(60.0f));
		Landing.Board->SetAirAttitude(Board, true, FRotator(0.0f, 90.0f, 0.0f).Quaternion());
		Landing.Tick();
		const FLandingVerdict Verdict = Landing.Board->GetLastLandingVerdict();
		TestNearlyEqual(TEXT("Tilt 60: the evaluator saw the attitude's tilt (deg)"), Landing.Board->GetLastLandingInputs().TiltDeg, 60.0f, 0.5f);
		TestEqual(TEXT("Tilt 60: crash"), Verdict.Grade, ELandingGrade::Crash);
		TestTrue(TEXT("Tilt 60: under- or over-rotated"), Verdict.Cause == ELandingCause::UnderRotated || Verdict.Cause == ELandingCause::OverRotated);
		TestFalse(TEXT("The root kept its heading only, no tilt (deg)"), FMath::Abs(Landing.Pawn->GetActorRotation().Roll) > 0.01f);
	}
	// The rider upside down over a flat board: inverted.
	{
		FBoardLanding Landing(0.0f, FVector(Speed, 0.0f, -50.0f));
		if (!TestTrue(TEXT("Fixture created"), Landing.IsValid())) { return false; }
		Landing.Board->SetAirAttitude(FQuat::Identity, true, FQuat(FVector::ForwardVector, PI));
		Landing.Tick();
		TestEqual(TEXT("Rider inverted: crash"), Landing.Board->GetLastLandingVerdict().Grade, ELandingGrade::Crash);
		TestEqual(TEXT("Rider inverted: cause"), Landing.Board->GetLastLandingVerdict().Cause, ELandingCause::Inverted);
	}
	// The attitude set but not active: the board keeps its old air orientation and lands as it did.
	{
		FBoardLanding Landing(0.0f, FVector(Speed, 0.0f, -50.0f));
		if (!TestTrue(TEXT("Fixture created"), Landing.IsValid())) { return false; }
		Landing.Board->SetAirAttitude(FQuat(FVector::ForwardVector, PI), false, FQuat(FVector::ForwardVector, PI));
		TestFalse(TEXT("Inactive attitude: not used"), Landing.Board->IsAirAttitudeActive());
		Landing.Tick();
		TestTrue(TEXT("Inactive attitude: a straight landing is not a crash"), Landing.Board->GetLastLandingVerdict().Grade != ELandingGrade::Crash && !Landing.Board->IsCrashing());
	}
	return true;
}

// In the air the drawn board is the attitude's and the physics root keeps only its heading; the feet
// stay in the straps upside down; the camera stays level and its pivot straight above the root; the
// drawn rider hands over without a jump; back on the water the drawn board is on the root again.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickBoardVisualFollowsAttitude, "KiteSurf.Trick.BoardVisualFollowsAttitude", TrickRotationTest::Flags)

bool FKiteSurfTrickBoardVisualFollowsAttitude::RunTest(const FString& Parameters)
{
	using namespace TrickRotationTest;
	const FTrickJumpResult R = RunTrickJump(FVector2D(1.0f, 0.0f));
	AddInfo(FString::Printf(TEXT("Back roll visuals: camera roll %.4f deg, pivot off %.2f cm, drawn board off the attitude %.2f deg, root tilt %.4f deg, feet off the straps %.3f cm (inverted %.3f cm), pelvis jump %.1f cm (take-off %.1f, landing %.1f)"),
		R.MaxCameraRollDeg, R.MaxPivotErrorCm, R.MaxVisualOffAttitudeDeg, R.MaxRootTiltDeg, R.WorstFeetOffStrapsCm, R.WorstFeetOffStrapsInvertedCm,
		R.MaxPelvisJumpCm, R.PelvisJumpAtTakeoffCm, R.PelvisJumpAtLandingCm));
	TestTrue(TEXT("The rider took off and landed"), R.bValid && R.bTookOff && R.bLanded);
	TestTrue(TEXT("The roll went over"), R.Inversions >= 1);
	TestTrue(FString::Printf(TEXT("The camera never rolls (%.4f deg)"), R.MaxCameraRollDeg), R.MaxCameraRollDeg < 0.1f);
	TestTrue(FString::Printf(TEXT("Its pivot stays straight above the root (%.2f cm off)"), R.MaxPivotErrorCm), R.MaxPivotErrorCm < 1.0f);
	TestTrue(FString::Printf(TEXT("The drawn board follows the attitude's board, within a step of rotation (%.2f deg)"), R.MaxVisualOffAttitudeDeg), R.MaxVisualOffAttitudeDeg < 3.0f);
	TestTrue(FString::Printf(TEXT("The physics root keeps only a heading (pitch and roll %.4f deg)"), R.MaxRootTiltDeg), R.MaxRootTiltDeg < 0.01f);
	TestTrue(FString::Printf(TEXT("The feet are in the drawn board's straps (%.3f cm)"), R.WorstFeetOffStrapsCm), R.WorstFeetOffStrapsCm < 0.5f);
	TestTrue(FString::Printf(TEXT("also upside down (%.3f cm)"), R.WorstFeetOffStrapsInvertedCm), R.WorstFeetOffStrapsInvertedCm >= 0.0f && R.WorstFeetOffStrapsInvertedCm < 0.5f);
	// At 60 fps a 250 deg/s roll moves the pelvis about 6 cm a frame; before the hand-over and the
	// drawn load, the pop's crouch release alone moved it 28 cm in the take-off frame.
	TestTrue(FString::Printf(TEXT("The drawn pelvis never moves more than 10 cm in a frame against the root (%.1f cm; take-off %.1f, landing %.1f)"), R.MaxPelvisJumpCm, R.PelvisJumpAtTakeoffCm, R.PelvisJumpAtLandingCm),
		R.MaxPelvisJumpCm < 10.0f);
	TestTrue(TEXT("A second after the landing the drawn board is the root's transform"), R.bVisualOnRootAfter);
	TestFalse(TEXT("with no override"), R.bOverrideAfter);
	return true;
}

// The rotation comes out the same at 30, 60 and 120 frames a second: the attitude is stepped inside
// the fixed step, and the inputs go in at the same simulation time.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickRotationStepRateIndependent, "KiteSurf.Trick.RotationStepRateIndependent", TrickRotationTest::Flags)

bool FKiteSurfTrickRotationStepRateIndependent::RunTest(const FString& Parameters)
{
	using namespace TrickRotationTest;
	const float FrameRates[3] = { 30.0f, 60.0f, 120.0f };
	FTrickJumpResult Results[3];
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Results[Index] = RunTrickJump(FVector2D(1.0f, 0.0f), 1.0f / FrameRates[Index]);
		AddInfo(FString::Printf(TEXT("%.0f fps: %s"), FrameRates[Index], *Describe(Results[Index])));
		TestTrue(FString::Printf(TEXT("%.0f fps: took off and landed"), FrameRates[Index]), Results[Index].bTookOff && Results[Index].bLanded);
	}
	for (int32 A = 0; A < 3; ++A)
	{
		for (int32 B = A + 1; B < 3; ++B)
		{
			const FString Pair = FString::Printf(TEXT("%.0f against %.0f fps"), FrameRates[A], FrameRates[B]);
			const FTrickJumpResult& RA = Results[A];
			const FTrickJumpResult& RB = Results[B];
			TestEqual(FString::Printf(TEXT("%s: the same inversions"), *Pair), RA.Inversions, RB.Inversions);
			TestNearlyEqual(FString::Printf(TEXT("%s: the same airtime (s)"), *Pair), RA.LandingAirtime, RB.LandingAirtime, 0.01f);
			TestNearlyEqual(FString::Printf(TEXT("%s: the same board tilt at touchdown (deg)"), *Pair), RA.LandingInputs.TiltDeg, RB.LandingInputs.TiltDeg, 0.5f);
			TestNearlyEqual(FString::Printf(TEXT("%s: the same yaw at touchdown (deg)"), *Pair), RA.LandingInputs.YawOffVelocityDeg, RB.LandingInputs.YawOffVelocityDeg, 0.5f);
			TestNearlyEqual(FString::Printf(TEXT("%s: the same rider up at touchdown"), *Pair), RA.LandingInputs.BodyUpDot, RB.LandingInputs.BodyUpDot, 0.01f);
			TestEqual(FString::Printf(TEXT("%s: the same grade"), *Pair), RA.Verdict.Grade, RB.Verdict.Grade);
		}
	}
	return true;
}

// The travel align on the bare component: with no trick going on the board comes round to the
// flight, either end first; a committed rotation or the stick turns it off.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickTravelAlignKeepsBoardAlongFlight, "KiteSurf.Trick.TravelAlignKeepsBoardAlongFlight", TrickRotationTest::Flags)

bool FKiteSurfTrickTravelAlignKeepsBoardAlongFlight::RunTest(const FString& Parameters)
{
	using namespace TrickRotationTest;
	const float Dt = 1.0f / 240.0f;
	auto MakeAir = [](float BodyYawDeg)
	{
		FAttitudeInputs In;
		In.bAirborne = true;
		In.SlavedBodyQuat = FRotator(0.0f, BodyYawDeg, 0.0f).Quaternion();
		In.SlavedBoardQuat = In.SlavedBodyQuat * URiderAttitudeComponent::MakeCanonicalStrapOffset(-1.0f);
		In.VelocityCmS = FVector(800.0f, 0.0f, 0.0f);
		In.HeightAboveWaterCm = 1.0e6f;
		return In;
	};
	auto YawOffTravelDeg = [](const URiderAttitudeComponent* A)
	{
		const FVector Nose = A->GetBoardQuat().GetAxisX();
		return LandingEvaluator::FoldYawDeg(FMath::RadiansToDegrees(FMath::Atan2(Nose.Y, Nose.X)));
	};

	// The board 40 deg off the flight (the nose along body -Right, so the body faces 90 + 40 deg).
	{
		URiderAttitudeComponent* A = NewObject<URiderAttitudeComponent>();
		const FAttitudeInputs In = MakeAir(90.0f + 40.0f);
		A->Step(Dt, In);
		const float Start = YawOffTravelDeg(A);
		for (int32 I = 0; I < 2 * 240; ++I) { A->Step(Dt, In); }
		AddInfo(FString::Printf(TEXT("Travel align: %.1f deg off the flight, 2 s later %.2f deg, |w| %.3f rad/s"), Start, YawOffTravelDeg(A), A->GetAngularVelocity().Size()));
		TestTrue(TEXT("Starts 40 deg off"), FMath::IsNearlyEqual(Start, 40.0f, 0.5f));
		TestTrue(TEXT("Two seconds later within 3 deg of the flight"), YawOffTravelDeg(A) < 3.0f);
	}
	// 140 deg off is 40 deg off tail first: it comes round to tail first, the short way.
	{
		URiderAttitudeComponent* A = NewObject<URiderAttitudeComponent>();
		const FAttitudeInputs In = MakeAir(90.0f + 140.0f);
		double Turned = 0.0;
		for (int32 I = 0; I < 2 * 240; ++I) { A->Step(Dt, In); Turned += A->GetAngularVelocity().Z * Dt; }
		TestTrue(TEXT("Tail first: within 3 deg of the flight"), YawOffTravelDeg(A) < 3.0f);
		TestTrue(FString::Printf(TEXT("the short way round (turned %.1f deg)"), FMath::RadiansToDegrees(Turned)), FMath::Abs(FMath::RadiansToDegrees(Turned)) < 60.0);
	}
	// A pre-wind commits a rotation: no align torque.
	{
		URiderAttitudeComponent* A = NewObject<URiderAttitudeComponent>();
		FAttitudeInputs In = MakeAir(90.0f + 40.0f);
		In.PreWindStick = FVector2D(1.0f, 0.0f);
		In.PreWindAmount = 1.0f;
		In.TakeoffLoad = 1.0f;
		for (int32 I = 0; I < 120; ++I) { A->Step(Dt, In); }
		TestTrue(TEXT("With a rotation committed the travel align is off"), A->GetLastStepDebug().TravelAlignTorqueNm.IsZero());
	}
	// The stick held: no align torque.
	{
		URiderAttitudeComponent* A = NewObject<URiderAttitudeComponent>();
		FAttitudeInputs In = MakeAir(90.0f + 40.0f);
		In.RotationStick = FVector2D(0.5f, 0.0f);
		In.bRotationInput = true;
		for (int32 I = 0; I < 120; ++I) { A->Step(Dt, In); }
		TestTrue(TEXT("With the stick held the travel align is off"), A->GetLastStepDebug().TravelAlignTorqueNm.IsZero());
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
