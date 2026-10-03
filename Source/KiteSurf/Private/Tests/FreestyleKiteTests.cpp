// The kite side of unhooked freestyle (docs/tricks/T3.md 1.3, T3.1 PR 1): the low park, the flick and
// the leash on UKiteComponent. The pawn does not switch them yet (T3.1 PR 2), so these tests do.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "BoardMovementComponent.h"
#include "Engine/World.h"
#include "KiteComponent.h"
#include "KiteRiderPawn.h"
#include "KiteSurf.h"
#include "KiteSurfGameMode.h"
#include "KiteSurfUnits.h"
#include "WindComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	const float FreestyleDeltaTime = 1.0f / 60.0f;
	// The unhooked chicken loop rides up to the stopper; T3.1 PR 2 locks the sheet there
	// (UnhookedStopperSheet, docs/tricks/T3.md 1.2). Until then the tests set it.
	const float StopperSheet = 0.6f;

	/** A pawn in a throwaway world, started the way the game mode starts a ride, in steady wind along +X, the kite drifting with the bar centred. */
	struct FFreestyleRideFixture
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UKiteComponent* Kite = nullptr;
		UBoardMovementComponent* Board = nullptr;

		explicit FFreestyleRideFixture(float WindKnots)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
			if (Pawn)
			{
				Kite = Pawn->GetKite();
				Board = Pawn->GetBoardMovement();
				if (UWindComponent* Wind = Pawn->GetWind())
				{
					Wind->BaseWind = FVector(KiteUnits::KnotsToCmS(WindKnots), 0.0f, 0.0f);
					Wind->GustStrength = 0.0f;
					Wind->DirectionDriftDeg = 0.0f;
				}
				AKiteSurfGameMode::InitializeRide(Pawn, KiteUnits::KnotsToCmS(12.0f), 1.0f);
				if (Kite)
				{
					Kite->bParkHoldAssist = false;
				}
			}
		}

		~FFreestyleRideFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		bool IsValid() const { return Pawn && Kite && Board; }

		float BodyWeightN() const { return Board->MassKg * KiteUnits::GravityMS2; }

		void Simulate(float Seconds)
		{
			const int32 Steps = FMath::RoundToInt(Seconds / FreestyleDeltaTime);
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				Pawn->Tick(FreestyleDeltaTime);
			}
		}
	};

	/** A rider standing still in steady wind along +X; only the kite is stepped (UpdateKite), so SetRiderAirborne stays as the test sets it. */
	struct FFreestyleStandingFixture
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UKiteComponent* Kite = nullptr;

		explicit FFreestyleStandingFixture(float WindKnots)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
			if (Pawn)
			{
				Kite = Pawn->GetKite();
				if (UWindComponent* Wind = Pawn->GetWind())
				{
					Wind->BaseWind = FVector(KiteUnits::KnotsToCmS(WindKnots), 0.0f, 0.0f);
					Wind->GustStrength = 0.0f;
					Wind->DirectionDriftDeg = 0.0f;
				}
				if (Kite)
				{
					Kite->SheetKite(StopperSheet);
				}
			}
		}

		~FFreestyleStandingFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		float BodyWeightN() const
		{
			const UBoardMovementComponent* Board = Pawn ? Pawn->GetBoardMovement() : nullptr;
			return (Board ? Board->MassKg : 85.0f) * KiteUnits::GravityMS2;
		}

		/** Flies the kite with the bar centred; returns the highest tension seen. */
		float Fly(float Seconds)
		{
			float PeakN = 0.0f;
			const int32 Steps = FMath::RoundToInt(Seconds / FreestyleDeltaTime);
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				Kite->UpdateKite(FreestyleDeltaTime);
				PeakN = FMath::Max(PeakN, Kite->GetLineTensionN());
			}
			return PeakN;
		}
	};
}

// The assists are off by default and leave the kite alone: no trim offset, not leashed.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfKiteFreestyleAssistsOffByDefault, "KiteSurf.Kite.FreestyleAssistsOffByDefault", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfKiteFreestyleAssistsOffByDefault::RunTest(const FString& Parameters)
{
	FFreestyleStandingFixture Standing(15.0f);
	UKiteComponent* Kite = Standing.Kite;
	TestNotNull(TEXT("Kite created"), Kite);
	if (!Kite)
	{
		return false;
	}
	TestFalse(TEXT("The low park is off by default"), Kite->IsLowParkAssistOn());
	TestFalse(TEXT("The kite is not leashed by default"), Kite->IsLeashed());
	Standing.Fly(1.0f);
	TestEqual(TEXT("No trim offset by default"), Kite->GetTrimOffsetDeg(), 0.0f);
	TestEqual(TEXT("No flick running"), Kite->GetFlickSecondsLeft(), 0.0f);

	// A flick offsets the trim for its length and no longer.
	Kite->RequestFlick(0.3f);
	Standing.Fly(0.2f);
	TestNearlyEqual(TEXT("A flick drops the trim by FlickTrimDropDeg"), Kite->GetTrimOffsetDeg(), -Kite->FlickTrimDropDeg, 1e-4f);
	Standing.Fly(0.2f);
	TestEqual(TEXT("and gives it back when it is over"), Kite->GetTrimOffsetDeg(), 0.0f);
	return true;
}

// Unhooked, the kite parks low instead of drifting to 12: with the low park on, a riding rider's kite
// sits at 45 +- 7 deg after 8 s in 15 kn and pulls under one body weight; without it the kite drifts
// above 60 deg. The low park also beats the airborne overhead hold.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfKiteUnhookedParksLow, "KiteSurf.Kite.UnhookedParksLow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfKiteUnhookedParksLow::RunTest(const FString& Parameters)
{
	const float ParkElevationDeg = 45.0f;
	const float ToleranceDeg = 7.0f;
	const float RideSeconds = 8.0f;

	float ElevationOn = 0.0f;
	float ElevationOff = 0.0f;
	for (const bool bLowPark : { true, false })
	{
		FFreestyleRideFixture Ride(15.0f);
		TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
		if (!Ride.IsValid())
		{
			return false;
		}
		Ride.Pawn->SheetKite(StopperSheet);
		Ride.Kite->SetLowParkAssist(bLowPark, ParkElevationDeg);
		float PeakLateN = 0.0f;
		Ride.Simulate(RideSeconds - 2.0f);
		for (float Elapsed = 0.0f; Elapsed < 2.0f; Elapsed += FreestyleDeltaTime)
		{
			Ride.Simulate(FreestyleDeltaTime);
			PeakLateN = FMath::Max(PeakLateN, Ride.Kite->GetLineTensionN());
		}
		const float ElevationDeg = Ride.Kite->GetElevationDeg();
		const float TensionBW = Ride.Kite->GetLineTensionN() / Ride.BodyWeightN();
		UE_LOG(LogKiteSurf, Log, TEXT("UnhookedParksLow (low park %d, 15 kn, %.0f m2): after %.0f s elevation %.1f deg, clock %.1f, depth %.1f, tension %.0f N (%.2f BW, peak over the last 2 s %.2f BW), %.1f kn, planing %d"),
			bLowPark, Ride.Kite->AreaM2, RideSeconds, ElevationDeg, Ride.Kite->GetClockDeg(), Ride.Kite->GetWindowDepthDeg(), Ride.Kite->GetLineTensionN(), TensionBW, PeakLateN / Ride.BodyWeightN(),
			Ride.Board->Velocity.Size2D() / KiteUnits::KnotsToCmS(1.0f), Ride.Board->IsPlaning());
		if (bLowPark)
		{
			ElevationOn = ElevationDeg;
			TestNearlyEqual(TEXT("Low park on: the kite sits at 45 deg after 8 s"), ElevationDeg, ParkElevationDeg, ToleranceDeg);
			TestTrue(FString::Printf(TEXT("Low park on: tension under 1.0 body weights (%.2f, peak %.2f over the last 2 s)"), TensionBW, PeakLateN / Ride.BodyWeightN()), PeakLateN < Ride.BodyWeightN());
			TestTrue(TEXT("Low park on: the lines are tight"), Ride.Kite->AreLinesTaut());
			TestFalse(TEXT("Low park on: the kite is flying"), Ride.Kite->IsCrashed());

			// A pop from the low park: the pawn tells the kite the rider is airborne, and the kite
			// stays low through the jump rather than being flown overhead.
			Ride.Pawn->Jump();
			float AirSeconds = 0.0f;
			float HighestInAirDeg = 0.0f;
			for (float Elapsed = 0.0f; Elapsed < 2.0f; Elapsed += FreestyleDeltaTime)
			{
				Ride.Simulate(FreestyleDeltaTime);
				if (Ride.Board->GetBoardState() == EBoardState::Airborne)
				{
					AirSeconds += FreestyleDeltaTime;
					HighestInAirDeg = FMath::Max(HighestInAirDeg, Ride.Kite->GetElevationDeg());
				}
			}
			UE_LOG(LogKiteSurf, Log, TEXT("UnhookedParksLow (low park, pop): %.2f s in the air, the kite at most %.1f deg while airborne"), AirSeconds, HighestInAirDeg);
			TestTrue(FString::Printf(TEXT("The pop leaves the water (%.2f s airborne)"), AirSeconds), AirSeconds > 0.2f);
			TestTrue(FString::Printf(TEXT("and the kite stays low while the rider is in the air (at most %.1f deg)"), HighestInAirDeg), HighestInAirDeg < ParkElevationDeg + ToleranceDeg);
		}
		else
		{
			ElevationOff = ElevationDeg;
			TestTrue(FString::Printf(TEXT("Low park off: the kite drifts above 60 deg (%.1f)"), ElevationDeg), ElevationDeg > 60.0f);
		}
	}
	TestTrue(FString::Printf(TEXT("The low park holds the kite strictly lower than the drift (%.1f against %.1f deg)"), ElevationOn, ElevationOff), ElevationOn < ElevationOff);

	// In the air: the overhead hold would fly the kite to 12; the low park keeps it at 45.
	float AirElevationOn = 0.0f;
	float AirElevationOff = 0.0f;
	for (const bool bLowPark : { true, false })
	{
		FFreestyleStandingFixture Standing(15.0f);
		UKiteComponent* Kite = Standing.Kite;
		TestNotNull(TEXT("Kite created"), Kite);
		if (!Kite)
		{
			return false;
		}
		Kite->SetWindowPosition(30.0f, 5.0f);
		Kite->SetRiderAirborne(true);
		Kite->SetLowParkAssist(bLowPark, ParkElevationDeg);
		Standing.Fly(RideSeconds);
		const float ElevationDeg = Kite->GetElevationDeg();
		UE_LOG(LogKiteSurf, Log, TEXT("UnhookedParksLow (rider airborne, low park %d): after %.0f s elevation %.1f deg, clock %.1f, tension %.0f N"),
			bLowPark, RideSeconds, ElevationDeg, Kite->GetClockDeg(), Kite->GetLineTensionN());
		TestTrue(TEXT("Airborne: the lines are tight"), Kite->AreLinesTaut());
		if (bLowPark)
		{
			AirElevationOn = ElevationDeg;
			TestNearlyEqual(TEXT("Airborne, low park on: the kite sits at 45 deg"), ElevationDeg, ParkElevationDeg, ToleranceDeg);
		}
		else
		{
			AirElevationOff = ElevationDeg;
			TestTrue(FString::Printf(TEXT("Airborne, low park off: the overhead hold flies it above 60 deg (%.1f)"), ElevationDeg), ElevationDeg > 60.0f);
		}
	}
	TestTrue(FString::Printf(TEXT("Airborne, the low park wins over the overhead hold (%.1f against %.1f deg)"), AirElevationOn, AirElevationOff), AirElevationOn < AirElevationOff);

	// The side: a kite at 12 parks on LowParkSide.
	{
		FFreestyleStandingFixture Standing(15.0f);
		UKiteComponent* Kite = Standing.Kite;
		if (!Kite)
		{
			return false;
		}
		Kite->SetWindowPosition(0.0f, 5.0f);
		Kite->LowParkSide = -1.0f;
		Kite->SetLowParkAssist(true, ParkElevationDeg);
		Standing.Fly(RideSeconds);
		UE_LOG(LogKiteSurf, Log, TEXT("UnhookedParksLow (from 12, LowParkSide -1): clock %.1f, elevation %.1f deg"), Kite->GetClockDeg(), Kite->GetElevationDeg());
		TestTrue(FString::Printf(TEXT("From 12 the kite parks on LowParkSide (clock %.1f)"), Kite->GetClockDeg()), Kite->GetClockDeg() < -20.0f);
		TestNearlyEqual(TEXT("at 45 deg"), Kite->GetElevationDeg(), ParkElevationDeg, ToleranceDeg);
	}
	return true;
}

// The bar is lost: the canopy flags on the leash, the rider feels under 150 N within 1.5 s, and the
// kite does not fall straight into the water. A leashed kite in the water stays there until the leash
// is cleared.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfKiteLeashFlagsKite, "KiteSurf.Kite.LeashFlagsKite", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfKiteLeashFlagsKite::RunTest(const FString& Parameters)
{
	FFreestyleRideFixture Ride(20.0f);
	TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}
	UKiteComponent* Kite = Ride.Kite;
	Ride.Pawn->SheetKite(StopperSheet);
	Kite->SetLowParkAssist(true, 45.0f);
	Ride.Simulate(5.0f);
	const float TensionBeforeN = Kite->GetLineTensionN();
	TestTrue(FString::Printf(TEXT("The kite pulls before the bar is lost (%.0f N)"), TensionBeforeN), TensionBeforeN > 300.0f);

	Kite->SetLeashed(true);
	TestTrue(TEXT("Leashed"), Kite->IsLeashed());
	TestEqual(TEXT("The leash flags the canopy"), Kite->GetTrimOffsetDeg(), Kite->LeashTrimDeg);
	float SecondsUnder150 = -1.0f;
	float SecondsKiteUnder400 = -1.0f;
	bool bCrashedEarly = false;
	float MaxFeltN = 0.0f;
	float CrashSeconds = -1.0f;
	const float WatchSeconds = 1.5f;
	for (float Elapsed = FreestyleDeltaTime; Elapsed <= WatchSeconds + KINDA_SMALL_NUMBER; Elapsed += FreestyleDeltaTime)
	{
		Ride.Simulate(FreestyleDeltaTime);
		MaxFeltN = FMath::Max(MaxFeltN, Kite->GetLineTensionN());
		if (SecondsUnder150 < 0.0f && Kite->GetLineTensionN() < 150.0f)
		{
			SecondsUnder150 = Elapsed;
		}
		if (SecondsKiteUnder400 < 0.0f && Kite->GetLastStepDebug().TensionN < 400.0f)
		{
			SecondsKiteUnder400 = Elapsed;
		}
		if (Kite->IsCrashed() && CrashSeconds < 0.0f)
		{
			CrashSeconds = Elapsed;
		}
		if (Elapsed <= 0.5f && Kite->IsCrashed())
		{
			bCrashedEarly = true;
		}
	}
	UE_LOG(LogKiteSurf, Log, TEXT("LeashFlagsKite (20 kn, %.0f m2): %.0f N before; felt under 150 N after %.3f s, the kite's own line tension under 400 N after %.3f s, most felt %.0f N, crashed after %.2f s (-1: not in %.1f s), elevation now %.1f deg"),
		Kite->AreaM2, TensionBeforeN, SecondsUnder150, SecondsKiteUnder400, MaxFeltN, CrashSeconds, WatchSeconds, Kite->GetElevationDeg());
	TestTrue(FString::Printf(TEXT("Tension the rider feels falls under 150 N within 1.5 s (%.3f s)"), SecondsUnder150), SecondsUnder150 >= 0.0f && SecondsUnder150 <= 1.5f);
	TestTrue(FString::Printf(TEXT("The canopy itself depowers, not just the cap: its line tension falls under 400 N within 1.5 s (%.3f s)"), SecondsKiteUnder400), SecondsKiteUnder400 >= 0.0f && SecondsKiteUnder400 <= 1.5f);
	TestTrue(FString::Printf(TEXT("The rider never feels more than LeashTensionCapN (%.0f N)"), MaxFeltN), MaxFeltN <= Kite->LeashTensionCapN + 0.5f);
	TestFalse(TEXT("The kite is not in the water within 0.5 s"), bCrashedEarly);

	// Left on the leash, it ends up in the water and stays there.
	Ride.Simulate(8.0f);
	TestTrue(TEXT("A leashed kite comes down in the water"), Kite->IsCrashed());
	Ride.Simulate(Kite->RelaunchDelaySeconds + 1.0f);
	TestTrue(TEXT("and does not relaunch while leashed"), Kite->IsCrashed());

	// The rider gets the bar back: the kite relaunches as usual and flies with the bar's trim.
	Kite->SetLeashed(false);
	Kite->SetLowParkAssist(false);
	TestEqual(TEXT("Unleashed, no trim offset"), Kite->GetTrimOffsetDeg(), 0.0f);
	Ride.Simulate(Kite->RelaunchDelaySeconds + 1.0f);
	TestFalse(TEXT("Unleashed, the kite relaunches"), Kite->IsCrashed());
	return true;
}

// The flick: parked at 45 deg in 20 kn, standing, a 0.3 s flick drops the pull by at least 40% (slack
// for a handle pass), then the kite flies on.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfKiteFlickDropsTension, "KiteSurf.Kite.FlickDropsTension", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfKiteFlickDropsTension::RunTest(const FString& Parameters)
{
	FFreestyleStandingFixture Standing(20.0f);
	UKiteComponent* Kite = Standing.Kite;
	TestNotNull(TEXT("Kite created"), Kite);
	if (!Kite)
	{
		return false;
	}
	Kite->SetWindowPosition(45.0f, 5.0f);
	Kite->SetLowParkAssist(true, 45.0f);
	Standing.Fly(7.0f);
	const float PeakParkedN = Standing.Fly(1.0f);
	const float ParkedElevationDeg = Kite->GetElevationDeg();
	TestNearlyEqual(TEXT("Parked at 45 deg before the flick"), ParkedElevationDeg, 45.0f, 7.0f);

	const float FlickSecondsAsked = 0.3f;
	Kite->RequestFlick(FlickSecondsAsked);
	float LowestN = PeakParkedN;
	float PeakDuringFlickN = 0.0f;
	float SecondsToLowest = 0.0f;
	bool bSlack = false;
	const float WindowSeconds = 0.45f;
	for (float Elapsed = FreestyleDeltaTime; Elapsed <= WindowSeconds + KINDA_SMALL_NUMBER; Elapsed += FreestyleDeltaTime)
	{
		Standing.Fly(FreestyleDeltaTime);
		const float TensionN = Kite->GetLineTensionN();
		if (Elapsed <= FlickSecondsAsked + KINDA_SMALL_NUMBER)
		{
			PeakDuringFlickN = FMath::Max(PeakDuringFlickN, TensionN);
		}
		if (TensionN < LowestN)
		{
			LowestN = TensionN;
			SecondsToLowest = Elapsed;
		}
		bSlack |= !Kite->AreLinesTaut();
	}
	const float Drop = 1.0f - LowestN / FMath::Max(PeakParkedN, 1.0f);
	TestEqual(TEXT("The flick is over"), Kite->GetFlickSecondsLeft(), 0.0f);

	// Then it flies on: back to pulling, still in the air.
	const float PeakAfterN = [&]() { Standing.Fly(1.5f); return Standing.Fly(0.5f); }();
	UE_LOG(LogKiteSurf, Log, TEXT("FlickDropsTension (20 kn, standing, %.0f m2): parked at %.1f deg with a peak of %.0f N (%.2f BW); the flick takes it to %.0f N after %.3f s (a %.0f%% drop; the most it pulls during the flick %.0f N), slack %d; 2 s later %.0f N at %.1f deg"),
		Kite->AreaM2, ParkedElevationDeg, PeakParkedN, PeakParkedN / Standing.BodyWeightN(), LowestN, SecondsToLowest, Drop * 100.0f, PeakDuringFlickN, bSlack, PeakAfterN, Kite->GetElevationDeg());
	TestTrue(FString::Printf(TEXT("A 0.3 s flick drops the pull by at least 40%% (%.0f%%)"), Drop * 100.0f), Drop >= 0.4f);
	TestTrue(FString::Printf(TEXT("and holds it down for the whole flick: its peak is at least 40%% under the parked peak (%.0f N against %.0f N)"), PeakDuringFlickN, PeakParkedN), PeakDuringFlickN <= 0.6f * PeakParkedN);
	TestFalse(TEXT("The kite is not in the water after the flick"), Kite->IsCrashed());
	TestTrue(FString::Printf(TEXT("It pulls again afterwards (%.0f N against %.0f N parked)"), PeakAfterN, PeakParkedN), PeakAfterN > 0.7f * PeakParkedN);
	TestTrue(FString::Printf(TEXT("and stays above 20 deg (%.1f)"), Kite->GetElevationDeg()), Kite->GetElevationDeg() > 20.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
