#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "BoardMovementComponent.h"
#include "KiteComponent.h"
#include "KiteMotionBar.h"
#include "KiteRiderPawn.h"
#include "KiteSurf.h"
#include "KiteSurfGameMode.h"
#include "KiteSurfUnits.h"
#include "WindComponent.h"
#include "Tricks/BarState.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/RiderAttitudeComponent.h"
#include "Tricks/TrickTrackerComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// Unhooked riding wired into the pawn (docs/tricks/T3.md T3.1 PR 2 and PR 3): the hook toggle, the
// sheet held at the stopper with the bar moving the arms, the low park, the grip limit losing the bar,
// the pass stepped after the kite, and the unhooked pop measured against a ballistic hop.

namespace TrickUnhookTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float FrameSeconds60 = 1.0f / 60.0f;

	/** A rider on the water in steady wind along +X, started on a beam reach as the game mode does. */
	struct FUnhookRide
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UKiteComponent* Kite = nullptr;
		UBoardMovementComponent* Board = nullptr;
		UTrickTrackerComponent* Tracker = nullptr;
		float FrameSeconds = FrameSeconds60;

		/** KiteM2 0: the size recommended for the wind. */
		explicit FUnhookRide(float WindKnots, float KiteM2 = 0.0f, float InFrameSeconds = FrameSeconds60)
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
			Tracker = Pawn->GetTrickTracker();
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
			Kite->bParkHoldAssist = false;
			Kite->SetKiteSize(KiteM2 > 0.0f ? KiteM2 : UKiteComponent::RecommendKiteSizeM2(WindKnots));
			// Sampled positions are the simulation's, not the drawn ones between steps.
			Pawn->bInterpolateRendering = false;
		}

		~FUnhookRide()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		bool IsValid() const { return Pawn && Kite && Board && Tracker; }

		float BodyWeightN() const { return Board->MassKg * KiteUnits::GravityMS2; }

		void Frame() { Pawn->Tick(FrameSeconds); }

		bool HasReached(float SimSeconds) const { return Pawn->GetSimTimeSeconds() + 0.5f * Pawn->SimStepSeconds >= SimSeconds; }

		void SimulateUntil(float SimSeconds)
		{
			while (!HasReached(SimSeconds))
			{
				Frame();
			}
		}

		bool IsAirborne() const { return Board->GetBoardState() == EBoardState::Airborne; }

		/** Presses the hook button and runs one frame, which hands the press to the bar. */
		void PressHook()
		{
			Pawn->PressHook();
			Frame();
		}
	};

	/** What an unhooked (or hooked) pop did. */
	struct FPopResult
	{
		bool bTookOff = false;
		bool bLanded = false;
		bool bLostBar = false;
		bool bHookedAtPop = true;
		/** Board's own numbers for the jump: apex above the take-off (m) and take-off to touchdown (s). */
		float ApexM = 0.0f;
		float AirtimeS = 0.0f;
		/** 8 h / t^2 (m/s^2): g for a ballistic hop. */
		float EffectiveGravity = 0.0f;
		/** In the air: the line's vertical pull and its tension (body weights), mean and least; the kite's elevation (deg). */
		float MeanLiftBW = 0.0f;
		float MeanTensionBW = 0.0f;
		float MinTensionBW = 100.0f;
		float MeanElevationDeg = 0.0f;
		float ElevationAtPopDeg = 0.0f;
		float TensionAtPopBW = 0.0f;
		float PeakTensionOnWaterBW = 0.0f;
		float KiteSheetAtPop = 0.0f;
		float SpeedAtPopKnots = 0.0f;
		float ElevationAtLandingDeg = 0.0f;
		ELandingGrade Grade = ELandingGrade::Clean;
		ELandingCause Cause = ELandingCause::None;
		FString TrickName;
	};

	/**
	 * docs/tricks/T3.md T3.1 PR 3's pop: ride, unhook (or not) at 0.5 s, ride on to 8 s, then load for
	 * 0.5 s with the weight back and let go; follows the jump to the touchdown. bLog writes a CSV line
	 * per frame to LogKiteSurf ("unhookpop,..."), the PR 3 telemetry.
	 */
	FPopResult RunPop(FUnhookRide& Ride, bool bUnhook, bool bLog, const TCHAR* Label, bool bHookedSamePop = false)
	{
		FPopResult Out;
		Ride.SimulateUntil(0.5f);
		if (bUnhook)
		{
			Ride.PressHook();
		}
		else if (bHookedSamePop)
		{
			// The same pop hooked in: the kite held where the unhooked rider's low park holds it and the
			// bar where the stopper holds it, until the pop; then the bar centred lets it drift up.
			Ride.Kite->SetLowParkAssist(true, Ride.Pawn->LowParkElevationDeg);
			Ride.Pawn->SheetKite(Ride.Pawn->UnhookedStopperSheet);
		}
		while (!Ride.HasReached(8.0f))
		{
			Ride.Frame();
			Out.PeakTensionOnWaterBW = FMath::Max(Out.PeakTensionOnWaterBW, Ride.Kite->GetLineTensionN() / Ride.BodyWeightN());
		}
		Ride.Board->SetWeightShift(-1.0f);
		Ride.Pawn->SetLoadHeld(true);
		const float LoadUntil = Ride.Pawn->GetSimTimeSeconds() + 0.5f;
		while (!Ride.HasReached(LoadUntil) && !Ride.IsAirborne())
		{
			Ride.Frame();
		}
		Out.bHookedAtPop = Ride.Pawn->IsHooked();
		Out.ElevationAtPopDeg = Ride.Kite->GetElevationDeg();
		Out.TensionAtPopBW = Ride.Kite->GetLineTensionN() / Ride.BodyWeightN();
		Out.KiteSheetAtPop = Ride.Kite->Sheet;
		Out.SpeedAtPopKnots = KiteUnits::CmSToKnots(static_cast<float>(Ride.Board->Velocity.Size2D()));
		const int32 JumpsBefore = Ride.Board->GetJumpCount();
		const int32 RecordsBefore = Ride.Tracker->GetJumpRecordCount();
		Ride.Pawn->ReleaseLoadAndPop();
		Ride.Board->SetWeightShift(0.0f);
		if (bHookedSamePop)
		{
			Ride.Kite->SetLowParkAssist(false, Ride.Pawn->LowParkElevationDeg);
		}

		const float GiveUpAt = Ride.Pawn->GetSimTimeSeconds() + 6.0f;
		int32 AirFrames = 0;
		float LiftSum = 0.0f;
		float TensionSum = 0.0f;
		float ElevationSum = 0.0f;
		if (bLog)
		{
			UE_LOG(LogKiteSurf, Display, TEXT("unhookpop,label,t_s,z_cm,vz_cms,lift_bw,tension_bw,elevation_deg,sheet,state,place"));
		}
		while (!Ride.HasReached(GiveUpAt) && Ride.Board->GetJumpCount() == JumpsBefore)
		{
			Ride.Frame();
			const bool bAir = Ride.IsAirborne();
			Out.bTookOff |= bAir;
			const float BW = Ride.BodyWeightN();
			const float LiftBW = KiteUnits::UnrealForceToN(static_cast<float>(Ride.Kite->GetLineForce().Z)) / BW;
			const float TensionBW = Ride.Kite->GetLineTensionN() / BW;
			if (bAir)
			{
				++AirFrames;
				LiftSum += LiftBW;
				TensionSum += TensionBW;
				ElevationSum += Ride.Kite->GetElevationDeg();
				Out.MinTensionBW = FMath::Min(Out.MinTensionBW, TensionBW);
			}
			Out.bLostBar |= Ride.Pawn->IsBarLost();
			if (bLog)
			{
				UE_LOG(LogKiteSurf, Display, TEXT("unhookpop,%s,%.4f,%.1f,%.1f,%.3f,%.3f,%.1f,%.2f,%d,%d"), Label, Ride.Pawn->GetSimTimeSeconds(),
					Ride.Pawn->GetActorLocation().Z, Ride.Board->Velocity.Z, LiftBW, TensionBW, Ride.Kite->GetElevationDeg(), Ride.Kite->Sheet,
					static_cast<int32>(Ride.Board->GetBoardState()), static_cast<int32>(Ride.Pawn->GetBarState().Place));
			}
		}
		Out.bLanded = Ride.Board->GetJumpCount() != JumpsBefore;
		if (AirFrames > 0)
		{
			Out.MeanLiftBW = LiftSum / AirFrames;
			Out.MeanTensionBW = TensionSum / AirFrames;
			Out.MeanElevationDeg = ElevationSum / AirFrames;
		}
		if (Out.bLanded)
		{
			Out.ApexM = KiteUnits::CmToM(Ride.Board->GetLastJumpApexHeight());
			Out.AirtimeS = Ride.Board->GetLastJumpAirtime();
			Out.EffectiveGravity = Out.AirtimeS > 0.0f ? 8.0f * Out.ApexM / (Out.AirtimeS * Out.AirtimeS) : 0.0f;
			Out.ElevationAtLandingDeg = Ride.Board->GetLastLandingInputs().KiteElevationDeg;
			Out.Grade = Ride.Board->GetLastLandingVerdict().Grade;
			Out.Cause = Ride.Board->GetLastLandingVerdict().Cause;
		}
		FJumpRecord Record;
		if (Ride.Tracker->GetJumpRecordCount() > RecordsBefore && Ride.Tracker->GetLastJumpRecord(Record))
		{
			Out.TrickName = Record.TrickName;
		}
		return Out;
	}

	FString Describe(const FPopResult& R)
	{
		return FString::Printf(TEXT("apex %.2f m, airtime %.2f s, 8h/t^2 %.2f m/s^2; in the air: lift %.2f BW, tension %.2f BW (least %.2f), kite at %.0f deg (%.0f at the pop, sheet %.2f), tension at the pop %.2f BW, peak on the water %.2f BW; %s%s"),
			R.ApexM, R.AirtimeS, R.EffectiveGravity, R.MeanLiftBW, R.MeanTensionBW, R.MinTensionBW, R.MeanElevationDeg, R.ElevationAtPopDeg, R.KiteSheetAtPop,
			R.TensionAtPopBW, R.PeakTensionOnWaterBW, *R.TrickName, R.bLostBar ? TEXT(", BAR LOST") : TEXT(""))
			+ FString::Printf(TEXT("; popped at %.1f kn, landed %s (%s) with the kite at %.0f deg"), R.SpeedAtPopKnots, *UEnum::GetDisplayValueAsText(R.Grade).ToString(), *UEnum::GetDisplayValueAsText(R.Cause).ToString(), R.ElevationAtLandingDeg);
	}
}



namespace TrickUnhookTest
{
	/** A controller whose reading the test sets: held still at a roll and pitch (BarControlTests' trace source). */
	class FHeldMotionSource : public IKiteMotionSource
	{
	public:
		virtual bool Poll(FKiteMotionSample& OutSample) override
		{
			const float Roll = FMath::DegreesToRadians(RollDeg);
			const float Pitch = FMath::DegreesToRadians(PitchDeg);
			OutSample.AccelG = FVector(-FMath::Sin(Roll) * FMath::Cos(Pitch), FMath::Cos(Roll) * FMath::Cos(Pitch), FMath::Sin(Pitch)).GetSafeNormal();
			OutSample.GyroRadS = FVector::ZeroVector;
			return true;
		}

		virtual FString GetDeviceName() const override { return TEXT("Held Controller"); }

		float RollDeg = 0.0f;
		float PitchDeg = 30.0f;
	};

	/** Pops hooked or not and returns once the rider is in the air (false if they never left the water). */
	bool PopIntoTheAir(FUnhookRide& Ride, float LoadSeconds = 0.5f)
	{
		Ride.Board->SetWeightShift(-1.0f);
		Ride.Pawn->SetLoadHeld(true);
		const float LoadUntil = Ride.Pawn->GetSimTimeSeconds() + LoadSeconds;
		while (!Ride.HasReached(LoadUntil) && !Ride.IsAirborne())
		{
			Ride.Frame();
		}
		Ride.Pawn->ReleaseLoadAndPop();
		Ride.Board->SetWeightShift(0.0f);
		const float GiveUp = Ride.Pawn->GetSimTimeSeconds() + 0.5f;
		while (!Ride.IsAirborne() && !Ride.HasReached(GiveUp))
		{
			Ride.Frame();
		}
		return Ride.IsAirborne();
	}

	/** Steps until the board counts a landing (or crash landing); false if it did not within Seconds. */
	bool FlyToTouchdown(FUnhookRide& Ride, int32 JumpsBefore, float Seconds = 6.0f)
	{
		const float GiveUp = Ride.Pawn->GetSimTimeSeconds() + Seconds;
		while (Ride.Board->GetJumpCount() == JumpsBefore && !Ride.HasReached(GiveUp))
		{
			Ride.Frame();
		}
		return Ride.Board->GetJumpCount() != JumpsBefore;
	}
}

using namespace TrickUnhookTest;

// PR 3, measure first (docs/tricks/T3.md T3.1 PR 3): the unhooked pop's telemetry, a CSV line per frame
// ("unhookpop,...") of the height, the line's lift and tension in body weights and the kite's elevation,
// next to the same pop hooked in with the kite left to drift up in the air.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickUnhookedPopTelemetry, "KiteSurf.Trick.UnhookedPopTelemetry", Flags)

bool FKiteSurfTrickUnhookedPopTelemetry::RunTest(const FString& Parameters)
{
	for (const bool bUnhook : { true, false })
	{
		FUnhookRide Ride(20.0f, 9.0f);
		if (!TestTrue(TEXT("Ride set up"), Ride.IsValid()))
		{
			return false;
		}
		const TCHAR* Label = bUnhook ? TEXT("unhooked") : TEXT("hooked");
		const FPopResult R = RunPop(Ride, bUnhook, true, Label, !bUnhook);
		AddInfo(FString::Printf(TEXT("%s pop, 20 kn, 9 m2: %s"), Label, *Describe(R)));
		TestTrue(FString::Printf(TEXT("The %s pop left the water and landed"), Label), R.bTookOff && R.bLanded);
		TestEqual(FString::Printf(TEXT("%s at the pop"), Label), R.bHookedAtPop, !bUnhook);
	}
	return true;
}

// tricks.md 6.4 and T3.1 PR 3: 20 kn, 9 m2, unhooked, 8 s of riding, loaded for 0.5 s with the weight
// back and let go: a near-ballistic hop of 1 to 4 m with 8h/t^2 within 30% of g, the bar kept, and the
// same at 30, 60 and 120 frames a second (the fixed step) to within 3% of the apex.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickUnhookedPopIsBallistic, "KiteSurf.Trick.UnhookedPopIsBallistic", Flags)

bool FKiteSurfTrickUnhookedPopIsBallistic::RunTest(const FString& Parameters)
{
	const float FrameRates[] = { 60.0f, 30.0f, 120.0f };
	float ApexAt60 = 0.0f;
	for (const float Fps : FrameRates)
	{
		FUnhookRide Ride(20.0f, 9.0f, 1.0f / Fps);
		if (!TestTrue(TEXT("Ride set up"), Ride.IsValid()))
		{
			return false;
		}
		const FPopResult R = RunPop(Ride, true, false, TEXT("ballistic"));
		const FString Rate = FString::Printf(TEXT("%.0f fps:"), Fps);
		AddInfo(FString::Printf(TEXT("%s %s"), *Rate, *Describe(R)));
		TestFalse(Rate + TEXT(" unhooked at the pop"), R.bHookedAtPop);
		TestTrue(Rate + TEXT(" left the water and landed"), R.bTookOff && R.bLanded);
		TestFalse(Rate + TEXT(" the bar is not lost"), R.bLostBar);
		TestTrue(FString::Printf(TEXT("%s apex %.2f m is 1 to 4 m"), *Rate, R.ApexM), R.ApexM >= 1.0f && R.ApexM <= 4.0f);
		TestTrue(FString::Printf(TEXT("%s 8h/t^2 %.2f m/s^2 is within 30%% of g"), *Rate, R.EffectiveGravity),
			FMath::Abs(R.EffectiveGravity - KiteUnits::GravityMS2) <= 0.3f * KiteUnits::GravityMS2);
		TestTrue(Rate + TEXT(" the kite's sheet is held at the stopper"), FMath::IsNearlyEqual(R.KiteSheetAtPop, Ride.Pawn->UnhookedStopperSheet, 1e-3f));
		if (Fps == 60.0f)
		{
			ApexAt60 = R.ApexM;
		}
		else
		{
			TestTrue(FString::Printf(TEXT("%s apex %.3f m within 3%% of 60 fps (%.3f m)"), *Rate, R.ApexM, ApexAt60), FMath::Abs(R.ApexM - ApexAt60) <= 0.03f * ApexAt60);
		}
	}
	return true;
}

// T3.1 PR 3: the same pop hooked in, the kite where the low park had it and the bar where the stopper
// holds it until the pop, then the bar centred so the kite drifts up over the rider (the airborne
// overhead hold): it floats, so 8h/t^2 is strictly smaller than unhooked, where the low park keeps the
// kite at 45 deg.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickHookedPopFloatsMore, "KiteSurf.Trick.HookedPopFloatsMore", Flags)

bool FKiteSurfTrickHookedPopFloatsMore::RunTest(const FString& Parameters)
{
	FUnhookRide UnhookedRide(20.0f, 9.0f);
	FUnhookRide HookedRide(20.0f, 9.0f);
	if (!TestTrue(TEXT("Rides set up"), UnhookedRide.IsValid() && HookedRide.IsValid()))
	{
		return false;
	}
	const FPopResult Unhooked = RunPop(UnhookedRide, true, false, TEXT("unhooked"));
	const FPopResult Hooked = RunPop(HookedRide, false, false, TEXT("hooked"), true);
	AddInfo(FString::Printf(TEXT("Unhooked: %s"), *Describe(Unhooked)));
	AddInfo(FString::Printf(TEXT("Hooked: %s"), *Describe(Hooked)));
	TestTrue(TEXT("Both pops landed"), Unhooked.bLanded && Hooked.bLanded);
	TestTrue(TEXT("The hooked pop was hooked in"), Hooked.bHookedAtPop);
	TestTrue(TEXT("Both pops left with the kite at the same height (within 3 deg)"), FMath::Abs(Unhooked.ElevationAtPopDeg - Hooked.ElevationAtPopDeg) <= 3.0f);
	TestTrue(FString::Printf(TEXT("Hooked 8h/t^2 %.2f is strictly smaller than unhooked %.2f m/s^2"), Hooked.EffectiveGravity, Unhooked.EffectiveGravity),
		Hooked.EffectiveGravity < Unhooked.EffectiveGravity);
	TestTrue(TEXT("The hooked kite drifted higher in the air"), Hooked.MeanElevationDeg > Unhooked.MeanElevationDeg);
	return true;
}

// tricks.md 6.4: 30 kn on a 12 m2 kite, unhooked, the bar steered over for 1.5 s: the grip limit pulls
// the bar from the hands within 2 s, the rider crashes (on the water at once), the kite hangs on its
// leash with under 150 N felt, and the kite does not lift the rider off the water in between.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickUnhookedOverpowerLosesBar, "KiteSurf.Trick.UnhookedOverpowerLosesBar", Flags)

bool FKiteSurfTrickUnhookedOverpowerLosesBar::RunTest(const FString& Parameters)
{
	FUnhookRide Ride(30.0f, 12.0f);
	if (!TestTrue(TEXT("Ride set up"), Ride.IsValid()))
	{
		return false;
	}
	Ride.SimulateUntil(0.5f);
	const int32 TakeoffsBefore = Ride.Board->GetTakeoffCount();
	Ride.PressHook();
	const float UnhookedAt = Ride.Pawn->GetSimTimeSeconds();
	TestFalse(TEXT("Unhooked"), Ride.Pawn->IsHooked());
	// The dive: the bar over towards the kite's own side sends it down through the power zone (the
	// plan's "steer -1" is for a kite on the left; this ride starts with it on the right).
	const float Dive = Ride.Kite->GetClockDeg() >= 0.0f ? 1.0f : -1.0f;
	Ride.Pawn->SteerKite(Dive);
	float LostAt = -1.0f;
	bool bCrashedAtLoss = false;
	int32 TakeoffsAtLoss = TakeoffsBefore;
	float PeakTensionBW = 0.0f;
	while (!Ride.HasReached(UnhookedAt + 2.5f) && LostAt < 0.0f)
	{
		if (Ride.HasReached(UnhookedAt + 1.5f))
		{
			Ride.Pawn->SteerKite(0.0f);
		}
		Ride.Frame();
		PeakTensionBW = FMath::Max(PeakTensionBW, Ride.Kite->GetLineTensionN() / Ride.BodyWeightN());
		if (Ride.Pawn->IsBarLost())
		{
			LostAt = Ride.Pawn->GetSimTimeSeconds();
			bCrashedAtLoss = Ride.Board->IsCrashing();
			TakeoffsAtLoss = Ride.Board->GetTakeoffCount();
		}
	}
	Ride.Pawn->SteerKite(0.0f);
	AddInfo(FString::Printf(TEXT("Bar lost %.2f s after unhooking (peak %.2f body weights)"), LostAt - UnhookedAt, PeakTensionBW));
	if (!TestTrue(TEXT("The bar is lost within 2 s of the dive"), LostAt >= 0.0f && LostAt - UnhookedAt <= 2.0f))
	{
		return true;
	}
	TestEqual(TEXT("Lost for the grip limit"), Ride.Pawn->GetBarState().LossCause, EBarLossCause::OverGrip);
	TestTrue(TEXT("Lost on the water: the rider crashes at once"), bCrashedAtLoss);
	TestEqual(TEXT("No kite-lift take-off between the unhook and the loss"), TakeoffsAtLoss, TakeoffsBefore);
	TestTrue(TEXT("The kite is on its leash"), Ride.Kite->IsLeashed());
	TestFalse(TEXT("The kite's low park is off"), Ride.Kite->IsLowParkAssistOn());
	// The crash puts the rider back on the board 1.5 s after it starts (and rehooks them): read the
	// leash just before that.
	Ride.SimulateUntil(LostAt + 1.4f);
	TestTrue(TEXT("Still on the leash 1.4 s later"), Ride.Kite->IsLeashed());
	TestTrue(FString::Printf(TEXT("The leashed kite pulls under 150 N 1.4 s later (%.0f N)"), Ride.Kite->GetLineTensionN()), Ride.Kite->GetLineTensionN() < 150.0f);
	return true;
}

// 15 kn on the recommended kite, 10 s unhooked: the bar is kept, the tension stays under the grip limit
// (1.3 body weights), the kite's sheet is held at the stopper and the low park holds it near 45 deg.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickUnhookedHoldsBelowLimit, "KiteSurf.Trick.UnhookedHoldsBelowLimit", Flags)

bool FKiteSurfTrickUnhookedHoldsBelowLimit::RunTest(const FString& Parameters)
{
	FUnhookRide Ride(15.0f);
	if (!TestTrue(TEXT("Ride set up"), Ride.IsValid()))
	{
		return false;
	}
	Ride.SimulateUntil(0.5f);
	Ride.PressHook();
	TestFalse(TEXT("Unhooked"), Ride.Pawn->IsHooked());
	TestTrue(TEXT("The low park is on"), Ride.Kite->IsLowParkAssistOn());
	const float Start = Ride.Pawn->GetSimTimeSeconds();
	float PeakBW = 0.0f;
	float ElevationSum = 0.0f;
	int32 ElevationFrames = 0;
	bool bSheetHeld = true;
	while (!Ride.HasReached(Start + 10.0f))
	{
		Ride.Frame();
		PeakBW = FMath::Max(PeakBW, Ride.Kite->GetLineTensionN() / Ride.BodyWeightN());
		bSheetHeld &= FMath::IsNearlyEqual(Ride.Kite->Sheet, Ride.Pawn->UnhookedStopperSheet, 1e-3f);
		if (Ride.HasReached(Start + 7.0f))
		{
			ElevationSum += Ride.Kite->GetElevationDeg();
			++ElevationFrames;
		}
	}
	const float MeanElevation = ElevationFrames > 0 ? ElevationSum / ElevationFrames : 0.0f;
	AddInfo(FString::Printf(TEXT("10 s unhooked at 15 kn on %.1f m2: peak %.2f body weights, kite at %.0f deg over the last 3 s"), Ride.Kite->AreaM2, PeakBW, MeanElevation));
	TestFalse(TEXT("The bar is kept"), Ride.Pawn->IsBarLost());
	TestFalse(TEXT("Still unhooked"), Ride.Pawn->IsHooked());
	TestTrue(FString::Printf(TEXT("Peak tension %.2f is under 1.3 body weights"), PeakBW), PeakBW < 1.3f);
	TestTrue(TEXT("The kite's sheet stayed at the stopper"), bSheetHeld);
	TestTrue(FString::Printf(TEXT("The low park holds the kite near 45 deg (%.0f)"), MeanElevation), FMath::Abs(MeanElevation - 45.0f) <= 8.0f);
	TestFalse(TEXT("Not crashed"), Ride.Board->IsCrashing());
	return true;
}

// The hook button on the pawn (KiteSurf.Trick.HookOnlyOnWater is the bar machine on its own) works on
// the water only: pressed in the air it is ignored (and not kept for the
// landing), and while crashing too; on the water it toggles.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickHookOnlyOnWaterOnPawn, "KiteSurf.Trick.HookOnlyOnWaterOnPawn", Flags)

bool FKiteSurfTrickHookOnlyOnWaterOnPawn::RunTest(const FString& Parameters)
{
	FUnhookRide Ride(20.0f, 9.0f);
	if (!TestTrue(TEXT("Ride set up"), Ride.IsValid()))
	{
		return false;
	}
	Ride.SimulateUntil(3.0f);
	const int32 JumpsBefore = Ride.Board->GetJumpCount();
	if (!TestTrue(TEXT("Popped into the air"), PopIntoTheAir(Ride)))
	{
		return true;
	}
	// Pressed through the Enhanced Input handler, as the button does.
	for (int32 Press = 0; Press < 3 && Ride.IsAirborne(); ++Press)
	{
		Ride.Pawn->OnHookPressed(FInputActionValue(true));
		Ride.Frame();
		TestTrue(TEXT("A hook press in the air is ignored"), Ride.Pawn->IsHooked());
	}
	TestTrue(TEXT("Landed"), FlyToTouchdown(Ride, JumpsBefore));
	Ride.SimulateUntil(Ride.Pawn->GetSimTimeSeconds() + 0.5f);
	TestTrue(TEXT("The presses in the air were not kept for the landing"), Ride.Pawn->IsHooked());
	TestFalse(TEXT("Back on the water"), Ride.IsAirborne());

	Ride.Pawn->OnHookPressed(FInputActionValue(true));
	Ride.Frame();
	TestFalse(TEXT("On the water the press unhooks"), Ride.Pawn->IsHooked());
	Ride.PressHook();
	TestTrue(TEXT("...and the next hooks in again"), Ride.Pawn->IsHooked());

	Ride.Board->TriggerCrash();
	Ride.PressHook();
	TestTrue(TEXT("A press while crashing is ignored"), Ride.Pawn->IsHooked());
	return true;
}

// A reset rehooks: the low park off, the bar's position back on the kite's sheet, the motion bar
// recentred. After a lost bar the crash's own reset takes the kite off its leash and gives the bar back.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickResetRehooks, "KiteSurf.Trick.ResetRehooks", Flags)

bool FKiteSurfTrickResetRehooks::RunTest(const FString& Parameters)
{
	FUnhookRide Ride(20.0f, 9.0f);
	if (!TestTrue(TEXT("Ride set up"), Ride.IsValid()))
	{
		return false;
	}
	Ride.SimulateUntil(1.0f);
	Ride.PressHook();
	TestFalse(TEXT("Unhooked"), Ride.Pawn->IsHooked());
	TestTrue(TEXT("Unhooked: the low park is on"), Ride.Kite->IsLowParkAssistOn());
	TestTrue(TEXT("Unhooked: the kite's sheet is at the stopper"), FMath::IsNearlyEqual(Ride.Kite->Sheet, Ride.Pawn->UnhookedStopperSheet, 1e-3f));
	const int32 RecentresBefore = Ride.Pawn->GetHookRecentreCount();

	Ride.Pawn->ResetRider();
	Ride.Frame();
	TestTrue(TEXT("The reset rehooks"), Ride.Pawn->IsHooked());
	TestFalse(TEXT("...the low park is off"), Ride.Kite->IsLowParkAssistOn());
	TestTrue(TEXT("...the kite's sheet is the bar's position again"), FMath::IsNearlyEqual(Ride.Kite->Sheet, Ride.Pawn->GetCurrentSheetInput(), 1e-3f));
	TestEqual(TEXT("...and the motion bar was recentred"), Ride.Pawn->GetHookRecentreCount(), RecentresBefore + 1);

	// Lose the bar on the water (a grip limit nobody can hold), then let the crash run its course.
	Ride.SimulateUntil(Ride.Pawn->GetSimTimeSeconds() + 1.0f);
	Ride.PressHook();
	Ride.Pawn->BarTunables.GripLimitBW = -1.0f;
	Ride.SimulateUntil(Ride.Pawn->GetSimTimeSeconds() + 0.3f);
	TestTrue(TEXT("The bar is lost"), Ride.Pawn->IsBarLost());
	TestTrue(TEXT("...the kite is on its leash"), Ride.Kite->IsLeashed());
	TestTrue(TEXT("...the rider crashes"), Ride.Board->IsCrashing());
	TestFalse(TEXT("...the board knows the bar is gone"), Ride.Board->IsRiderBarInHands());
	Ride.Pawn->BarTunables.GripLimitBW = FBarTunables().GripLimitBW;
	const int32 ResetsBefore = Ride.Board->GetResetCount();
	Ride.SimulateUntil(Ride.Pawn->GetSimTimeSeconds() + 2.0f);
	TestTrue(TEXT("The crash put the rider back on the board"), Ride.Board->GetResetCount() > ResetsBefore);
	TestTrue(TEXT("...hooked in"), Ride.Pawn->IsHooked());
	TestFalse(TEXT("...with the bar"), Ride.Pawn->IsBarLost());
	TestFalse(TEXT("...the kite off its leash"), Ride.Kite->IsLeashed());
	TestTrue(TEXT("...and the board knows"), Ride.Board->IsRiderBarInHands());
	return true;
}

// Bar lost in the air: the rider is not stopped mid-air (TriggerCrash would zero their climb); they fly
// on and the landing evaluator crashes them at the touchdown with the cause BarLost.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickBarLostInAirCrashesAtTouchdown, "KiteSurf.Trick.BarLostInAirCrashesAtTouchdown", Flags)

bool FKiteSurfTrickBarLostInAirCrashesAtTouchdown::RunTest(const FString& Parameters)
{
	FUnhookRide Ride(20.0f, 9.0f);
	if (!TestTrue(TEXT("Ride set up"), Ride.IsValid()))
	{
		return false;
	}
	Ride.SimulateUntil(0.5f);
	Ride.PressHook();
	Ride.SimulateUntil(8.0f);
	const int32 JumpsBefore = Ride.Board->GetJumpCount();
	const int32 RecordsBefore = Ride.Tracker->GetJumpRecordCount();
	if (!TestTrue(TEXT("Popped into the air unhooked"), PopIntoTheAir(Ride) && !Ride.Pawn->IsHooked()))
	{
		return true;
	}
	Ride.SimulateUntil(Ride.Pawn->GetSimTimeSeconds() + 0.1f);
	// A grip limit nobody can hold: the bar goes 0.15 s later, in the air.
	Ride.Pawn->BarTunables.GripLimitBW = -1.0f;
	const float GiveUp = Ride.Pawn->GetSimTimeSeconds() + 0.5f;
	while (!Ride.Pawn->IsBarLost() && !Ride.HasReached(GiveUp))
	{
		Ride.Frame();
	}
	TestTrue(TEXT("The bar is lost in the air"), Ride.Pawn->IsBarLost() && Ride.IsAirborne());
	TestFalse(TEXT("...without crashing the rider mid-air"), Ride.Board->IsCrashing());
	TestTrue(TEXT("...the kite on its leash"), Ride.Kite->IsLeashed());
	// Still flying: a crash would have set the climb to 0 and put the board in its landing state.
	Ride.Frame();
	TestTrue(TEXT("Still in the air a frame later"), Ride.IsAirborne() || Ride.Board->GetJumpCount() != JumpsBefore);
	TestTrue(TEXT("Touched down"), FlyToTouchdown(Ride, JumpsBefore));
	const FLandingVerdict Verdict = Ride.Board->GetLastLandingVerdict();
	TestEqual(TEXT("The landing is a crash"), Verdict.Grade, ELandingGrade::Crash);
	TestEqual(TEXT("...for the bar lost"), Verdict.Cause, ELandingCause::BarLost);
	FJumpRecord Record;
	if (TestTrue(TEXT("The jump was recorded"), Ride.Tracker->GetJumpRecordCount() > RecordsBefore && Ride.Tracker->GetLastJumpRecord(Record)))
	{
		TestEqual(TEXT("The record's cause is BarLost"), Record.LandingCause, ELandingCause::BarLost);
		TestFalse(TEXT("The record is unhooked"), Record.bHooked);
	}
	return true;
}

// Unhooked, every bar input moves the arms and not the kite's sheet (docs/tricks/T3.md 1.2 and the
// addendum's routing through SheetKite): the stick and keys (IA_Sheet, spring-loaded), a scripted
// SheetKite, and the motion bar. Hooked, the same inputs sheet the kite, as always.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickUnhookedArmExtensionRouting, "KiteSurf.Trick.UnhookedArmExtensionRouting", Flags)

bool FKiteSurfTrickUnhookedArmExtensionRouting::RunTest(const FString& Parameters)
{
	// The mapping: the bar's middle is the default extension, fully in the hips, fully out the arms straight.
	TestNearlyEqual(TEXT("Bar in the middle: the default extension"), AKiteRiderPawn::ArmExtensionForBar(0.5f, 0.5f, 0.7f), 0.7f, 1e-4f);
	TestNearlyEqual(TEXT("Bar fully in: at the hips"), AKiteRiderPawn::ArmExtensionForBar(1.0f, 0.5f, 0.7f), 0.0f, 1e-4f);
	TestNearlyEqual(TEXT("Bar fully out: arms out"), AKiteRiderPawn::ArmExtensionForBar(0.0f, 0.5f, 0.7f), 1.0f, 1e-4f);
	TestNearlyEqual(TEXT("Bar half way in: half way to the hips"), AKiteRiderPawn::ArmExtensionForBar(0.75f, 0.5f, 0.7f), 0.35f, 1e-4f);

	FUnhookRide Ride(15.0f);
	if (!TestTrue(TEXT("Ride set up"), Ride.IsValid()))
	{
		return false;
	}
	AKiteRiderPawn* Pawn = Ride.Pawn;
	Ride.SimulateUntil(1.0f);
	auto HoldSheetInput = [&Ride, Pawn](float Value, float Seconds)
	{
		Pawn->OnSheetTriggered(FInputActionValue(Value));
		Ride.SimulateUntil(Pawn->GetSimTimeSeconds() + Seconds);
	};

	// Hooked: the stick sheets the kite.
	HoldSheetInput(1.0f, 0.6f);
	TestTrue(FString::Printf(TEXT("Hooked, the stick pulls the kite's sheet in (%.2f)"), Ride.Kite->Sheet), Ride.Kite->Sheet > 0.9f);
	HoldSheetInput(0.0f, 0.6f);

	Ride.PressHook();
	const float Stopper = Pawn->UnhookedStopperSheet;
	TestFalse(TEXT("Unhooked"), Pawn->IsHooked());
	TestNearlyEqual(TEXT("Unhooked, the kite's sheet goes to the stopper"), Ride.Kite->Sheet, Stopper, 1e-3f);
	TestNearlyEqual(TEXT("Bar in the middle: the default arm extension"), Pawn->GetArmExtension(), Pawn->UnhookedArmExtensionDefault, 0.02f);

	// The stick and the keys (IA_Sheet).
	HoldSheetInput(1.0f, 0.6f);
	TestTrue(FString::Printf(TEXT("Stick pulled: the bar to the hips (arms %.2f)"), Pawn->GetArmExtension()), Pawn->GetArmExtension() < 0.05f);
	TestNearlyEqual(TEXT("...and the kite's sheet stays at the stopper"), Ride.Kite->Sheet, Stopper, 1e-3f);
	HoldSheetInput(-1.0f, 0.6f);
	TestTrue(FString::Printf(TEXT("Stick pushed: arms out (%.2f)"), Pawn->GetArmExtension()), Pawn->GetArmExtension() > 0.95f);
	TestNearlyEqual(TEXT("...and the kite's sheet stays at the stopper"), Ride.Kite->Sheet, Stopper, 1e-3f);
	HoldSheetInput(0.0f, 0.8f);
	TestNearlyEqual(TEXT("Let go: back to the default extension"), Pawn->GetArmExtension(), Pawn->UnhookedArmExtensionDefault, 0.02f);

	// Scripted, through SheetKite.
	Pawn->SheetKite(1.0f);
	Ride.Frame();
	TestNearlyEqual(TEXT("SheetKite(1): the bar at the hips"), Pawn->GetArmExtension(), 0.0f, 1e-3f);
	TestNearlyEqual(TEXT("...the kite's sheet at the stopper"), Ride.Kite->Sheet, Stopper, 1e-3f);
	Pawn->SheetKite(0.5f);
	Ride.Frame();

	// The motion bar: the pad tipped towards the player pulls the bar in, which moves the arms.
	TSharedPtr<FHeldMotionSource> Pad = MakeShared<FHeldMotionSource>();
	Pawn->SetMotionSource(Pad);
	Pawn->SetMotionBarEnabled(true);
	Ride.SimulateUntil(Pawn->GetSimTimeSeconds() + 1.0f);
	TestTrue(TEXT("The motion bar has the bar"), Pawn->IsMotionBarActive());
	const float ArmsBefore = Pawn->GetArmExtension();
	Pad->PitchDeg = 50.0f;
	Ride.SimulateUntil(Pawn->GetSimTimeSeconds() + 1.5f);
	AddInfo(FString::Printf(TEXT("Motion bar: arms %.2f -> %.2f, bar %.2f, kite sheet %.2f"), ArmsBefore, Pawn->GetArmExtension(), Pawn->GetCurrentSheetInput(), Ride.Kite->Sheet));
	TestTrue(TEXT("The pad tipped in brings the arms in"), Pawn->GetArmExtension() < ArmsBefore - 0.2f);
	TestNearlyEqual(TEXT("...and the kite's sheet stays at the stopper"), Ride.Kite->Sheet, Stopper, 1e-3f);
	return true;
}

// The motion bar recentres on every hook toggle (docs/tricks/T3.md 1.5): the pad as it is held then is
// the bar where it is, so nothing jumps however the pad had drifted.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfMotionBarUnhookRecentres, "KiteSurf.MotionBar.UnhookRecentres", Flags)

bool FKiteSurfMotionBarUnhookRecentres::RunTest(const FString& Parameters)
{
	FUnhookRide Ride(15.0f);
	if (!TestTrue(TEXT("Ride set up"), Ride.IsValid()))
	{
		return false;
	}
	AKiteRiderPawn* Pawn = Ride.Pawn;
	TSharedPtr<FHeldMotionSource> Pad = MakeShared<FHeldMotionSource>();
	Pad->PitchDeg = 30.0f;
	Pawn->SetMotionSource(Pad);
	Pawn->SetMotionBarEnabled(true);
	Ride.SimulateUntil(1.0f);
	if (!TestTrue(TEXT("The motion bar has the bar"), Pawn->IsMotionBarActive()))
	{
		return true;
	}

	for (int32 Toggle = 0; Toggle < 2; ++Toggle)
	{
		const FString What = Toggle == 0 ? TEXT("Unhooking") : TEXT("Hooking in");
		// The calibration has drifted: the pad's 30 deg is now read from a neutral of 10 deg, with the
		// bar where it is, so nothing moves until the toggle.
		const float Bar = Pawn->GetCurrentSheetInput();
		Pawn->MotionBarMapping.NeutralPitchDeg = 10.0f;
		Pawn->MotionBarMapping.SheetAtNeutral = Bar - 20.0f / Pawn->MotionBarMapping.SheetRangeDeg;
		const int32 RecentresBefore = Pawn->GetHookRecentreCount();
		Ride.PressHook();
		Ride.Frame();
		TestEqual(What + TEXT(" toggled the hook"), Pawn->IsHooked(), Toggle != 0);
		TestEqual(What + TEXT(" recentred the motion bar once"), Pawn->GetHookRecentreCount(), RecentresBefore + 1);
		TestNearlyEqual(What + TEXT(": the pad as it is held is the new neutral (deg)"), Pawn->MotionBarMapping.NeutralPitchDeg, Pad->PitchDeg, 2.0f);
		TestNearlyEqual(What + TEXT(": the bar did not jump"), Pawn->GetCurrentSheetInput(), Bar, 0.03f);
		Ride.SimulateUntil(Pawn->GetSimTimeSeconds() + 0.5f);
	}
	return true;
}

// Where the lines pull on the body (docs/tricks/T3.md 1.1), pure.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickLineAttachPoints, "KiteSurf.Trick.LineAttachPoints", Flags)

bool FKiteSurfTrickLineAttachPoints::RunTest(const FString& Parameters)
{
	const FLineAttachTunables T;
	const FVector Ahead = FVector(1.0f, 0.0f, 0.36f).GetSafeNormal();
	FBarState Hooked;
	TestTrue(TEXT("Hooked: the harness hook"), LineAttach::AttachPointBody(Hooked, Ahead, 1.0f, 1.0f, T).Equals(T.HookBodyCm, 1e-3f));

	FBarState Unhooked;
	Unhooked.bHooked = false;
	TestTrue(TEXT("Unhooked, arms in: the hands at the hips"), LineAttach::AttachPointBody(Unhooked, Ahead, 0.0f, 1.0f, T).Equals(T.HipHandsBodyCm, 1e-3f));
	const FVector Shoulders(T.ShoulderBodyCm.X, 0.0f, T.ShoulderBodyCm.Z);
	TestTrue(TEXT("Unhooked, arms out, line in the cone: arm's reach along the line from between the shoulders"),
		LineAttach::AttachPointBody(Unhooked, Ahead, 1.0f, 1.0f, T).Equals(Shoulders + Ahead * T.ArmReachCm, 1e-2f));
	const FVector Behind = FVector(-1.0f, 0.0f, 0.0f);
	const FVector Clamped = LineAttach::ClampToArmCone(Behind, Ahead, T.ArmConeDeg);
	TestNearlyEqual(TEXT("A line behind is clamped to the cone's edge (deg)"), static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(Clamped, Ahead)))), T.ArmConeDeg, 0.1f);
	FBarState OneHand = Unhooked;
	OneHand.Hands = EBarHands::FrontOnly;
	TestNearlyEqual(TEXT("Front hand only, nose on the right: from the right shoulder (cm)"),
		static_cast<float>(LineAttach::AttachPointBody(OneHand, Ahead, 1.0f, 1.0f, T).Y), static_cast<float>(FMath::Abs(T.ShoulderBodyCm.Y)), 1e-2f);
	TestNearlyEqual(TEXT("...nose on the left: the left shoulder (cm)"),
		static_cast<float>(LineAttach::AttachPointBody(OneHand, Ahead, 1.0f, -1.0f, T).Y), -static_cast<float>(FMath::Abs(T.ShoulderBodyCm.Y)), 1e-2f);

	FBarState Blind = Unhooked;
	Blind.Place = EBarPlace::BehindBack;
	TestTrue(TEXT("Behind the back: the lower back"), LineAttach::AttachPointBody(Blind, Ahead, 1.0f, 1.0f, T).Equals(T.BehindBackBodyCm, 1e-3f));

	FBarState Passing = Unhooked;
	Passing.Place = EBarPlace::Passing;
	Passing.PassSense = 1.0f;
	Passing.PassT = 0.0f;
	const FVector Start = LineAttach::AttachPointBody(Passing, Ahead, 1.0f, 1.0f, T);
	Passing.PassT = 0.5f;
	const FVector Middle = LineAttach::AttachPointBody(Passing, Ahead, 1.0f, 1.0f, T);
	Passing.PassT = 1.0f;
	const FVector End = LineAttach::AttachPointBody(Passing, Ahead, 1.0f, 1.0f, T);
	TestTrue(TEXT("A backside pass starts at the front hand's hip (nose on the right)"), Start.Y > 0.0f && FMath::IsNearlyEqual(static_cast<float>(Start.X), T.PassHipXCm, 1e-2f));
	TestTrue(TEXT("...goes round the back"), FMath::IsNearlyEqual(static_cast<float>(Middle.X), T.PassBackXCm, 1e-2f) && FMath::Abs(Middle.Y) < 1e-2f);
	TestTrue(TEXT("...to the other hip"), End.Y < 0.0f && FMath::IsNearlyEqual(static_cast<float>(End.Y), -Start.Y, 1e-2f));

	FBarState Lost = Unhooked;
	Lost.Place = EBarPlace::Lost;
	TestTrue(TEXT("Lost: the leash pulls at the harness"), LineAttach::AttachPointBody(Lost, Ahead, 1.0f, 1.0f, T).Equals(T.HookBodyCm, 1e-3f));
	return true;
}

// The drawn bar follows the bar's state (docs/tricks/T3.md 1.4): unhooked it is at the hands, further
// out with the arms out; on the leash it hangs up the lines from the harness with the hands off it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfRiderUnhookedBarPose, "KiteSurf.Rider.UnhookedBarPose", Flags)

bool FKiteSurfRiderUnhookedBarPose::RunTest(const FString& Parameters)
{
	FUnhookRide Ride(15.0f);
	if (!TestTrue(TEXT("Ride set up"), Ride.IsValid()))
	{
		return false;
	}
	AKiteRiderPawn* Pawn = Ride.Pawn;
	Ride.SimulateUntil(1.0f);
	Ride.PressHook();
	auto BarFromPelvis = [Pawn]() { return static_cast<float>(FVector::Dist(Pawn->GetDrawnBarCentre(), Pawn->GetRiderRigPose().Pelvis)); };
	auto HandsToBar = [Pawn]()
	{
		const FRiderRigPose& Pose = Pawn->GetRiderRigPose();
		return static_cast<float>(FVector::Dist((Pose.Arms[0].End + Pose.Arms[1].End) * 0.5f, Pawn->GetDrawnBarCentre()));
	};
	Pawn->SheetKite(1.0f);
	Ride.SimulateUntil(Pawn->GetSimTimeSeconds() + 0.2f);
	const float AtHips = BarFromPelvis();
	Pawn->SheetKite(0.0f);
	Ride.SimulateUntil(Pawn->GetSimTimeSeconds() + 0.2f);
	const float ArmsOut = BarFromPelvis();
	AddInfo(FString::Printf(TEXT("Bar from the pelvis: %.0f cm at the hips, %.0f cm with the arms out; hands %.0f cm from the bar"), AtHips, ArmsOut, HandsToBar()));
	TestTrue(TEXT("Arms out puts the bar further from the body than at the hips"), ArmsOut > AtHips + 20.0f);
	TestTrue(TEXT("The hands are on the bar"), HandsToBar() < 8.0f);

	Pawn->BarTunables.GripLimitBW = -1.0f;
	Ride.SimulateUntil(Pawn->GetSimTimeSeconds() + 0.3f);
	if (!TestTrue(TEXT("The bar is lost"), Pawn->IsBarLost()))
	{
		return true;
	}
	const float FromHook = static_cast<float>(FVector::Dist(Pawn->GetDrawnBarCentre(), Pawn->GetHarnessHookWorldPosition()));
	TestNearlyEqual(TEXT("On the leash the bar hangs LeashLengthCm up the lines from the harness (cm)"), FromHook, Pawn->LeashLengthCm, 1.0f);
	TestTrue(TEXT("...with the hands off it"), HandsToBar() > 40.0f);
	return true;
}

// T3.4 on a ride: an unhooked pop with a backside spin (pre-wind and the air stick held), the pass
// pressed once when the back is to the kite (BackToKiteDeg within PassBackToKiteDeg), the stick let go
// once the bar is round: the bar goes round in the slack, the flick assist dips the kite, and the jump
// is named from the bar (SummariseJump, ApplyToSignature): "Backside 1 to blind". A backside 3 is not
// reachable on this pop: after the pass the lines' pull at the lower back swings the rider back to face
// away from the kite (held, the stick lands them with the lines wrapped), and the T1 spin rates give
// about 180 deg in the airtime.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickPassOnRide, "KiteSurf.Trick.PassOnRide", Flags)

bool FKiteSurfTrickPassOnRide::RunTest(const FString& Parameters)
{
	// At 60 and 30 frames a second: the fixed step makes the same ride, but the frame splits the steps
	// differently, which is where the bar once saw the board swap ends without the stance.
	for (const float Fps : { 60.0f, 30.0f })
	{
		FUnhookRide Ride(20.0f, 9.0f, 1.0f / Fps);
		if (!TestTrue(TEXT("Ride set up"), Ride.IsValid()))
		{
			continue;
		}
		AKiteRiderPawn* Pawn = Ride.Pawn;
		Ride.SimulateUntil(0.5f);
		Ride.PressHook();
		Ride.SimulateUntil(8.0f);
		const FVector2D Spin(1.0f, 1.0f); // x towards the back roll's side, y up: a flat backside spin
		Ride.Board->SetWeightShift(-1.0f);
		Pawn->SetLoadHeld(true);
		Pawn->SetPreWind(Spin);
		Ride.SimulateUntil(8.5f);
		const int32 JumpsBefore = Ride.Board->GetJumpCount();
		const int32 RecordsBefore = Ride.Tracker->GetJumpRecordCount();
		Pawn->ReleaseLoadAndPop();
		Ride.Board->SetWeightShift(0.0f);
		Pawn->SetPreWind(FVector2D::ZeroVector);
		Pawn->SetAirRotationInput(Spin);

		bool bPressed = false;
		bool bPassStarted = false;
		bool bFlicked = false;
		float PassStartTension = -1.0f;
		const float GiveUp = Pawn->GetSimTimeSeconds() + 5.0f;
		while (Ride.Board->GetJumpCount() == JumpsBefore && !Ride.HasReached(GiveUp))
		{
			const FVector LineDir = (Ride.Kite->GetKiteWorldPosition() - Pawn->GetActorLocation()).GetSafeNormal();
			const float BackToKite = BarStateMachine::BackToKiteDeg(Pawn->GetRiderAttitude()->GetBodyQuat(), LineDir);
			if (!bPressed && Ride.IsAirborne() && BackToKite <= Pawn->BarTunables.PassBackToKiteDeg)
			{
				// Through the Enhanced Input handler, as X does (batch A moved the keyboard binding
				// off LeftShift, which is IA_Rotate's now).
				Pawn->OnPassPressed(FInputActionValue(true));
				bPressed = true;
			}
			if (Pawn->GetBarState().JumpPasses.Num() > 0)
			{
				// The bar is round: stop spinning and let the landing assist line the board up.
				Pawn->SetAirRotationInput(FVector2D::ZeroVector);
			}
			Ride.Frame();
			if (!bPassStarted && Pawn->GetBarState().Place == EBarPlace::Passing)
			{
				bPassStarted = true;
				bFlicked = Ride.Kite->GetFlickSecondsLeft() > 0.0f;
				PassStartTension = Ride.Kite->GetLineTensionN() / Ride.BodyWeightN();
			}
		}
		Pawn->SetAirRotationInput(FVector2D::ZeroVector);
		TestTrue(TEXT("Pressed the pass in the air with the back to the kite"), bPressed);
		TestTrue(TEXT("The pass started"), bPassStarted);
		TestTrue(TEXT("...and the flick assist dipped the kite"), bFlicked);
		TestTrue(TEXT("Landed"), Ride.Board->GetJumpCount() != JumpsBefore);
		FJumpRecord Record;
		if (!TestTrue(TEXT("The jump was recorded"), Ride.Tracker->GetJumpRecordCount() > RecordsBefore && Ride.Tracker->GetLastJumpRecord(Record)))
		{
			continue;
		}
		AddInfo(FString::Printf(TEXT("%.0f fps: \"%s\": %d pass(es), the lines land %s, graded %s (%s), tension at the pass start %.2f BW"), Fps, *Record.TrickName, Record.Passes.Num(),
			*UEnum::GetDisplayValueAsText(Record.BarLandingStance).ToString(), *UEnum::GetDisplayValueAsText(Record.Grade).ToString(),
			*UEnum::GetDisplayValueAsText(Record.LandingCause).ToString(), PassStartTension));
		TestFalse(TEXT("Recorded unhooked"), Record.bHooked);
		if (TestEqual(TEXT("One pass in the record"), Record.Passes.Num(), 1))
		{
			TestEqual(TEXT("A backside pass"), Record.Passes[0].Sense, ETrickSense::Backside);
			TestEqual(TEXT("...in the air"), Record.Passes[0].Kind, ETrickPassKind::Air);
		}
		// A backside half turn with the bar passed round the back lands blind: 360 for the pass less the 180
		// the lines are still round (BarStateMachine::SummariseJump), one half turn.
		TestEqual(TEXT("Named from the bar: a backside 180 with a pass, landing blind"), Record.TrickName, FString(TEXT("Backside 1 to blind")));
		TestEqual(TEXT("The lines land blind"), Record.BarLandingStance, ETrickStance::Blind);
		TestNotEqual(TEXT("Not a crash"), Record.Grade, ELandingGrade::Crash);
		// Riding away blind (T3.5: the blind stance held for BlindHoldSeconds), the board swaps ends under the
		// rider and then they slide round to face the kite: the lines come back in front, they do not wrap.
		const float RideAwayUntil = Pawn->GetSimTimeSeconds() + Pawn->BlindHoldSeconds + 1.0f;
		bool bKept = true;
		while (!Ride.HasReached(RideAwayUntil))
		{
			Ride.Frame();
			bKept &= !Pawn->IsBarLost();
		}
		TestTrue(TEXT("The bar is kept riding away"), bKept);
		TestEqual(TEXT("...and is back in front"), Pawn->GetBarState().Place, EBarPlace::Front);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS


