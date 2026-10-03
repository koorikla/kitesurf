#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "BoardMovementComponent.h"
#include "KiteComponent.h"
#include "KiteRiderPawn.h"
#include "KiteSurf.h"
#include "KiteSurfGameMode.h"
#include "KiteSurfUnits.h"
#include "WindComponent.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/KiteLoopRecord.h"
#include "Tricks/TrickRecognition.h"
#include "Tricks/TrickTrackerComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// The kite's in-air loop entry (docs/tricks/T2.md T2.4, "Kite change"): with the rider in the air a
// full bar (UKiteComponent::AirLoopFullBarThreshold, 0.85) loops the kite its way from any clock
// position, and reversed mid-loop starts a loop the other way, so S-loops and contra loops can be
// flown. On the water the LoopClockDeg (35 deg) rule is unchanged. The fixtures are minimal copies of
// RideLoopTests.cpp's FStandingFixture and FRideFixture and of TrickFeedTests.cpp's timed send.
namespace TrickLoopEntryTestsLocal
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	const float EntryDeltaTime = 1.0f / 60.0f;

	/** A stationary rider in steady wind along +X; the kite is stepped on its own with UpdateKite, so the test says whether the rider is in the air. */
	struct FEntryStandingFixture
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UKiteComponent* Kite = nullptr;

		/** At 15 kn the kite is the pawn's default; with bRigForWind, the size and model a rider would rig for the wind, sheeted in. */
		explicit FEntryStandingFixture(float WindKnots = 15.0f, bool bRigForWind = false)
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
				Kite->SheetKite(0.7f);
				Kite->bParkHoldAssist = true;
				if (bRigForWind)
				{
					Kite->SetKiteModel(EKiteModel::Loop);
					Kite->SetKiteSize(UKiteComponent::RecommendKiteSizeM2(WindKnots));
					Kite->SheetKite(1.0f);
				}
			}
		}

		~FEntryStandingFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		/** Parks the kite at the window edge at this clock on the water, settled for Seconds with the bar centred. */
		void Park(float ClockDeg, float Seconds = 3.0f)
		{
			Kite->SetRiderAirborne(false);
			Kite->SetWindowPosition(ClockDeg, 10.0f);
			Fly(0.0f, Seconds);
		}

		void Fly(float Steer, float Seconds)
		{
			Kite->SteerKite(Steer);
			for (float Elapsed = 0.0f; Elapsed < Seconds; Elapsed += EntryDeltaTime)
			{
				Kite->UpdateKite(EntryDeltaTime);
			}
		}
	};

	/** What a held bar did: where the kite was when it started looping (the first step it was looping), and the loop records it made. */
	struct FEntry
	{
		bool bLooped = false;
		float StartClockDeg = 0.0f;
		float EntryClockDeg = 0.0f;
		float EntrySeconds = -1.0f;
		float EntrySide = 0.0f;
		/** Clock the kite had reached on the bar's side before it started looping (deg, most towards the bar's side). */
		float FurthestClockAlongBarDeg = -180.0f;
		bool bCompleted = false;
		int32 CompletedDirection = 0;
		float CompletedSeconds = -1.0f;
		bool bCrashed = false;
	};

	int32 FirstNewCompleted(const UKiteComponent* Kite, int32 FirstIndex, int32& OutDirection)
	{
		for (const FKiteLoopRecord& Record : Kite->GetLoopRecords())
		{
			if (Record.Index >= FirstIndex && Record.bCompleted)
			{
				OutDirection = Record.Direction;
				return Record.Index;
			}
		}
		return INDEX_NONE;
	}

	/** Holds Bar for up to Seconds, stopping once a new completed loop record appears. */
	FEntry HoldBar(FEntryStandingFixture& Standing, float Bar, float Seconds)
	{
		UKiteComponent* Kite = Standing.Kite;
		FEntry Out;
		Out.StartClockDeg = Kite->GetClockDeg();
		const float Side = Bar >= 0.0f ? 1.0f : -1.0f;
		const int32 FirstIndex = Kite->GetLoopRecordCount();
		Kite->SteerKite(Bar);
		for (float Elapsed = 0.0f; Elapsed < Seconds; Elapsed += EntryDeltaTime)
		{
			Kite->UpdateKite(EntryDeltaTime);
			if (!Out.bLooped)
			{
				Out.FurthestClockAlongBarDeg = FMath::Max(Out.FurthestClockAlongBarDeg, Kite->GetClockDeg() * Side);
			}
			if (!Out.bLooped && Kite->IsLooping())
			{
				Out.bLooped = true;
				Out.EntryClockDeg = Kite->GetClockDeg();
				Out.EntrySeconds = Elapsed + EntryDeltaTime;
				Out.EntrySide = Kite->GetLoopSide();
			}
			int32 Direction = 0;
			if (FirstNewCompleted(Kite, FirstIndex, Direction) != INDEX_NONE)
			{
				Out.bCompleted = true;
				Out.CompletedDirection = Direction;
				Out.CompletedSeconds = Elapsed + EntryDeltaTime;
				break;
			}
			if (Kite->IsCrashed())
			{
				Out.bCrashed = true;
				break;
			}
		}
		Kite->SteerKite(0.0f);
		return Out;
	}

	/** Records made since FirstIndex, as an S-loop or contra loop classifier sees them (times from the first record). */
	TArray<FJumpLoop> NewJumpLoops(const UKiteComponent* Kite, int32 FirstIndex, FString& OutText)
	{
		TArray<FJumpLoop> Result;
		float T0 = -1.0f;
		for (const FKiteLoopRecord& Record : Kite->GetLoopRecords())
		{
			if (Record.Index < FirstIndex)
			{
				continue;
			}
			if (T0 < 0.0f)
			{
				T0 = Record.StartTimeSeconds;
			}
			FJumpLoop JumpLoop;
			JumpLoop.Loop = Record;
			JumpLoop.StartSinceTakeoffSeconds = Record.StartTimeSeconds - T0 + 0.5f;
			JumpLoop.StartSinceApexSeconds = -1.0f;
			JumpLoop.RiderHeightAtStartCm = 500.0f;
			Result.Add(JumpLoop);
			OutText += FString::Printf(TEXT(" [#%d dir %+d %.0f deg %s%s, %.2f s from %.2f s, travel side %+d]"), Record.Index, Record.Direction, Record.TurnDeg,
				Record.bCompleted ? TEXT("complete") : TEXT("part"), Record.bKiteCrashed ? TEXT(" crashed") : TEXT(""), Record.DurationSeconds,
				Record.StartTimeSeconds, Record.RiderTravelSide);
		}
		return Result;
	}

	FString KindsOf(const TArray<FTrickLoop>& Loops)
	{
		TArray<FString> Parts;
		for (const FTrickLoop& L : Loops)
		{
			FString Part = StaticEnum<ETrickLoopKind>()->GetNameStringByValue(static_cast<int64>(L.Kind));
			Parts.Add(L.bContra ? Part + TEXT("(contra)") : Part);
		}
		return FString::Join(Parts, TEXT(","));
	}

	/** A pawn in a throwaway world, set up the way the game mode starts a ride, in steady wind along +X. */
	struct FEntryRideFixture
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UKiteComponent* Kite = nullptr;
		UBoardMovementComponent* Board = nullptr;
		UTrickTrackerComponent* Tracker = nullptr;

		explicit FEntryRideFixture(float WindKnots = 30.0f, float TackSide = 1.0f)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
			if (Pawn)
			{
				Kite = Pawn->GetKite();
				Board = Pawn->GetBoardMovement();
				Tracker = Pawn->GetTrickTracker();
				if (UWindComponent* Wind = Pawn->GetWind())
				{
					Wind->BaseWind = FVector(KiteUnits::KnotsToCmS(WindKnots), 0.0f, 0.0f);
					Wind->GustStrength = 0.0f;
					Wind->DirectionDriftDeg = 0.0f;
				}
				AKiteSurfGameMode::InitializeRide(Pawn, KiteUnits::KnotsToCmS(12.0f), TackSide);
				Kite->bParkHoldAssist = true;
				Pawn->bInterpolateRendering = false;
				Kite->SetKiteModel(EKiteModel::Loop);
				Kite->SetKiteSize(UKiteComponent::RecommendKiteSizeM2(WindKnots));
			}
		}

		~FEntryRideFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		bool IsValid() const { return Pawn && Kite && Board && Tracker; }

		void Simulate(float Seconds)
		{
			const int32 Steps = FMath::RoundToInt(Seconds / EntryDeltaTime);
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				Pawn->Tick(EntryDeltaTime);
			}
		}

		bool IsAirborne() const { return Board->GetBoardState() == EBoardState::Airborne; }
	};

	/** RideLoopTests' TimedReleaseSeconds: the release of the jump button after the send reaches the kite (s). */
	constexpr float EntryTimedReleaseSeconds = 0.66f;

	/**
	 * What a scripted air bar did in a real jump: the timed send and pop at 30 kn, the bar centred
	 * once in the air, then from AirBarDelaySeconds after take-off each (bar, until) phase in turn.
	 */
	struct FAirBarResult
	{
		bool bTookOff = false;
		bool bLanded = false;
		float AirtimeSeconds = 0.0f;
		float ApexCm = 0.0f;
		int32 FirstRecordIndex = 0;
		float TakeoffKiteSeconds = 0.0f;
		float LandingKiteSeconds = 0.0f;
		FJumpRecord Jump;
		bool bHasJump = false;
	};

	/** One phase of a scripted air bar: hold Bar until Done says so (or the rider lands). */
	struct FAirBarPhase
	{
		float Bar = 0.0f;
		TFunction<bool(const UKiteComponent*)> Done;
	};

	FAirBarResult RunAirBar(FEntryRideFixture& Ride, const TArray<FAirBarPhase>& Phases, float AirBarDelaySeconds = 0.5f)
	{
		FAirBarResult Out;
		Ride.Simulate(8.0f);
		Out.FirstRecordIndex = Ride.Kite->GetLoopRecordCount();

		Ride.Pawn->SteerKite(-1.0f);
		const float SendDeadTimeSeconds = Ride.Kite->GetSteeringDeadTimeSeconds();
		Ride.Board->SetWeightShift(-1.0f);
		Ride.Pawn->SetLoadHeld(true);

		bool bLeftWater = false;
		float AirSeconds = 0.0f;
		int32 Phase = -1;
		for (float Elapsed = 0.0f; Elapsed < 30.0f; Elapsed += EntryDeltaTime)
		{
			Ride.Simulate(EntryDeltaTime);
			const bool bAir = Ride.IsAirborne();
			if (bAir && !Out.bTookOff)
			{
				Out.bTookOff = true;
				Out.TakeoffKiteSeconds = Ride.Kite->GetSimTimeSeconds();
				Ride.Board->SetWeightShift(0.0f);
				Ride.Pawn->SetLoadHeld(false);
				Ride.Pawn->SheetKite(1.0f);
			}
			if (!bLeftWater && Elapsed >= EntryTimedReleaseSeconds + SendDeadTimeSeconds)
			{
				Ride.Board->SetWeightShift(-1.0f);
				Ride.Pawn->SheetKite(1.0f);
				Ride.Pawn->ReleaseLoadAndPop();
				Ride.Board->SetWeightShift(0.0f);
				bLeftWater = true;
			}
			if (bAir)
			{
				AirSeconds += EntryDeltaTime;
				Out.ApexCm = FMath::Max(Out.ApexCm, Ride.Board->GetCurrentJumpHeight());
			}
			if (Out.bTookOff && AirSeconds >= AirBarDelaySeconds && Phase < Phases.Num())
			{
				if (Phase < 0 || (Phases[Phase].Done && Phases[Phase].Done(Ride.Kite)))
				{
					++Phase;
				}
				Ride.Pawn->SteerKite(Phase < Phases.Num() ? Phases[Phase].Bar : 0.0f);
			}
			else if (Phase < 0 && (bAir || Ride.Kite->GetClockDeg() < 0.0f))
			{
				Ride.Pawn->SteerKite(0.0f); // the send is over: the assist flies the kite overhead
			}
			if (bAir && Ride.Board->Velocity.Z < 0.0f)
			{
				Ride.Pawn->SetLoadHeld(true); // coming down: crouch for the landing
			}
			if (Out.bTookOff && !bAir && AirSeconds > 0.3f)
			{
				Out.bLanded = true;
				Out.LandingKiteSeconds = Ride.Kite->GetSimTimeSeconds();
				break;
			}
		}
		Out.AirtimeSeconds = AirSeconds;
		Ride.Pawn->SetLoadHeld(false);
		Ride.Pawn->SteerKite(0.0f);
		Ride.Simulate(0.5f);
		Out.bHasJump = Ride.Tracker->GetLastJumpRecord(Out.Jump);
		return Out;
	}
}

// In the air a full bar held for AirLoopHoldSeconds (0.3 s) loops the kite its way from wherever it
// is. Parked 40 deg round on one side (or overhead), a full bar towards the other side used to fly it
// across over the top, as it still does on the water: the loop rule wanted the kite 35 deg round on
// the bar's side. With the rider in the air it now loops once the bar has been held 0.3 s at the
// kite (after the dead time), the bar's way, and completes. A 0.2 s full tap flies it across instead
// (arrow keys are always a full bar); a 0.4 s one loops it. With the air rule turned off (threshold above 1) or a bar of 0.6 the same start flies
// across first, as before. A full bar held since before the rider left the water is the send and
// does not loop until it has been eased: the timed jumps fly as they did.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfKiteAirLoopFromAnyClock, "KiteSurf.Kite.AirLoopFromAnyClock", TrickLoopEntryTestsLocal::Flags)

bool FKiteSurfKiteAirLoopFromAnyClock::RunTest(const FString& Parameters)
{
	// Not at file scope: in a unity build it would reach the other test files.
	using namespace TrickLoopEntryTestsLocal;
	struct FCase { float ClockDeg; float Bar; };
	const FCase Cases[] = { { 40.0f, -1.0f }, { -40.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, -1.0f } };
	for (const FCase& Case : Cases)
	{
		const FString Which = FString::Printf(TEXT("Parked at clock %+.0f, full bar %+.0f"), Case.ClockDeg, Case.Bar);

		// In the air: loops from where it is.
		{
			FEntryStandingFixture Standing;
			if (!TestNotNull(TEXT("Kite created"), Standing.Kite))
			{
				return false;
			}
			UKiteComponent* Kite = Standing.Kite;
			Standing.Park(Case.ClockDeg);
			const float DeadTimeSeconds = Kite->GetSteeringDeadTimeSeconds();
			Kite->SetRiderAirborne(true);
			const FEntry Air = HoldBar(Standing, Case.Bar, 8.0f);
			UE_LOG(LogKiteSurf, Log, TEXT("AirLoopFromAnyClock: %s, in the air: looping %d after %.2f s (dead time %.2f s) at clock %+.1f (from %+.1f), side %+.0f; completed %d dir %+d after %.2f s; crashed %d"),
				*Which, Air.bLooped, Air.EntrySeconds, DeadTimeSeconds, Air.EntryClockDeg, Air.StartClockDeg, Air.EntrySide, Air.bCompleted, Air.CompletedDirection, Air.CompletedSeconds, Air.bCrashed);
			TestTrue(Which + TEXT(", in the air: the kite loops"), Air.bLooped);
			const float HoldSeconds = Kite->AirLoopHoldSeconds;
			TestTrue(FString::Printf(TEXT("%s, in the air: once the bar has been held %.2f s at the kite (%.2f s, dead time %.2f s)"), *Which, HoldSeconds, Air.EntrySeconds, DeadTimeSeconds),
				Air.bLooped && Air.EntrySeconds >= DeadTimeSeconds + HoldSeconds - EntryDeltaTime && Air.EntrySeconds <= DeadTimeSeconds + HoldSeconds + 2.0f * EntryDeltaTime);
			TestTrue(FString::Printf(TEXT("%s, in the air: from about where it was parked (clock %+.1f)"), *Which, Air.EntryClockDeg), FMath::Abs(Air.EntryClockDeg - Case.ClockDeg) < 15.0f);
			TestEqual(Which + TEXT(", in the air: the bar's way"), Air.EntrySide, Case.Bar);
			TestTrue(FString::Printf(TEXT("%s, in the air: and completes a loop that way (%.2f s, dir %+d)"), *Which, Air.CompletedSeconds, Air.CompletedDirection),
				Air.bCompleted && Air.CompletedDirection == static_cast<int32>(Case.Bar));
			TestFalse(Which + TEXT(", in the air: the kite stays out of the water"), Air.bCrashed);
		}

		// The rule off, or a bar under the threshold: flies across as before.
		for (const bool bRuleOff : { true, false })
		{
			FEntryStandingFixture Standing;
			UKiteComponent* Kite = Standing.Kite;
			Standing.Park(Case.ClockDeg);
			Kite->SetRiderAirborne(true);
			float Bar = Case.Bar;
			if (bRuleOff)
			{
				Kite->AirLoopFullBarThreshold = 1.01f;
			}
			else
			{
				Bar = 0.6f * Case.Bar;
			}
			const FEntry Across = HoldBar(Standing, Bar, 2.0f);
			const FString How = Which + (bRuleOff ? TEXT(", the air rule off") : TEXT(", bar 0.6"));
			UE_LOG(LogKiteSurf, Log, TEXT("AirLoopFromAnyClock: %s: looping %d after %.2f s at clock %+.1f; reached %+.1f on the bar's side first"),
				*How, Across.bLooped, Across.EntrySeconds, Across.EntryClockDeg, Across.FurthestClockAlongBarDeg);
			TestTrue(FString::Printf(TEXT("%s: no loop until the kite is %.0f deg round on the bar's side (entered at %+.1f)"), *How, Kite->LoopClockDeg, Across.EntryClockDeg),
				!Across.bLooped || Across.EntryClockDeg * Case.Bar >= Kite->LoopClockDeg - 1.0f);
		}
	}

	// The send: a full bar held as the rider leaves the water flies the kite across as before; eased
	// and pulled again, it loops.
	{
		FEntryStandingFixture Standing;
		UKiteComponent* Kite = Standing.Kite;
		Standing.Park(40.0f);
		Standing.Fly(-1.0f, 0.4f); // the send, on the water: travelling, not looping
		const bool bLoopingOnWater = Kite->IsLooping();
		Kite->SetRiderAirborne(true);
		const FEntry Held = HoldBar(Standing, -1.0f, 0.4f);
		Standing.Fly(0.0f, 0.4f);
		const float EasedClockDeg = Kite->GetClockDeg();
		const FEntry Pulled = HoldBar(Standing, -1.0f, 1.0f);
		UE_LOG(LogKiteSurf, Log, TEXT("AirLoopFromAnyClock: send held through the take-off: looping on the water %d, held in the air %d (clock %+.1f); eased at clock %+.1f, pulled again: looping %d after %.2f s at %+.1f"),
			bLoopingOnWater, Held.bLooped, Held.EntryClockDeg, EasedClockDeg, Pulled.bLooped, Pulled.EntrySeconds, Pulled.EntryClockDeg);
		TestFalse(TEXT("The send on the water does not loop"), bLoopingOnWater);
		TestTrue(FString::Printf(TEXT("A send held through the take-off still flies across (looping %d at %+.1f)"), Held.bLooped, Held.EntryClockDeg),
			!Held.bLooped || Held.EntryClockDeg * -1.0f >= Kite->LoopClockDeg - 1.0f);
		TestTrue(TEXT("Eased and pulled again in the air, the bar loops the kite"), Pulled.bLooped && Pulled.EntrySide == -1.0f);
	}

	// The hold: in the air a full tap shorter than AirLoopHoldSeconds (0.3 s) flies the kite across as
	// any bar does, so arrow-key steering (always a full bar) still flies the kite; held 0.4 s it loops.
	for (const float TapSeconds : { 0.2f, 0.4f })
	{
		FEntryStandingFixture Standing;
		UKiteComponent* Kite = Standing.Kite;
		Standing.Park(40.0f);
		Kite->SetRiderAirborne(true);
		const float StartClockDeg = Kite->GetClockDeg();
		const FEntry Tap = HoldBar(Standing, -1.0f, TapSeconds);
		bool bLoopedAfter = false;
		Kite->SteerKite(0.0f);
		for (float Elapsed = 0.0f; Elapsed < 1.0f; Elapsed += EntryDeltaTime)
		{
			Kite->UpdateKite(EntryDeltaTime);
			bLoopedAfter |= Kite->IsLooping();
		}
		const bool bLooped = Tap.bLooped || bLoopedAfter;
		UE_LOG(LogKiteSurf, Log, TEXT("AirLoopFromAnyClock: a %.1f s full tap in the air from clock %+.1f (hold %.2f s): looped %d; the kite is at clock %+.1f a second later"),
			TapSeconds, StartClockDeg, Kite->AirLoopHoldSeconds, bLooped, Kite->GetClockDeg());
		if (TapSeconds < Kite->AirLoopHoldSeconds)
		{
			TestFalse(FString::Printf(TEXT("A %.1f s full tap in the air does not loop the kite"), TapSeconds), bLooped);
			TestTrue(FString::Printf(TEXT("and flies it towards the other side (clock %+.1f from %+.1f)"), Kite->GetClockDeg(), StartClockDeg), Kite->GetClockDeg() < StartClockDeg);
		}
		else
		{
			TestTrue(FString::Printf(TEXT("A %.1f s full bar in the air loops the kite"), TapSeconds), bLooped);
		}
	}
	return true;
}

// Half a loop one way, then the bar reversed at full: the loop ends and one the other way starts at
// once, from wherever the kite is. The kite's loop tracker splits the turn into two partial records in
// opposite directions, each of 180 deg or more and less than a whole turn, back to back, and
// TrickRecognition::ClassifyLoops names them an S-loop. Both ways round.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfKiteAirReverseMakesSLoop, "KiteSurf.Kite.AirReverseMakesSLoop", TrickLoopEntryTestsLocal::Flags)

bool FKiteSurfKiteAirReverseMakesSLoop::RunTest(const FString& Parameters)
{
	// Not at file scope: in a unity build it would reach the other test files.
	using namespace TrickLoopEntryTestsLocal;
	const float ReverseAtDeg = 200.0f;   // the first half's turn when the bar is reversed (the kite carries on through the dead time)
	const float ReleaseAtDeg = 190.0f;   // the second half's turn when the bar is centred
	const float SWindKnots = 25.0f;      // the kite rigged for it and sheeted in, as in a jump
	for (const float First : { 1.0f, -1.0f })
	{
		FEntryStandingFixture Standing(SWindKnots, true);
		if (!TestNotNull(TEXT("Kite created"), Standing.Kite))
		{
			return false;
		}
		UKiteComponent* Kite = Standing.Kite;
		Standing.Park(0.0f);
		Kite->SetRiderAirborne(true);
		const int32 FirstIndex = Kite->GetLoopRecordCount();
		const float DeadTimeSeconds = Kite->GetSteeringDeadTimeSeconds();
		const auto OpenTurn = [Kite](int32 Direction)
		{
			FKiteLoopRecord Open;
			return Kite->GetLoopDirection() == Direction && Kite->GetOpenLoop(Open) ? Open.TurnDeg : 0.0f;
		};

		// The first half.
		Kite->SteerKite(First);
		float Seconds = 0.0f;
		for (; Seconds < 6.0f && OpenTurn(static_cast<int32>(First)) < ReverseAtDeg && !Kite->IsCrashed(); Seconds += EntryDeltaTime)
		{
			Kite->UpdateKite(EntryDeltaTime);
		}
		const float FirstHalfSeconds = Seconds;
		const float ReverseClockDeg = Kite->GetClockDeg();

		// The reverse: the kite loops the other way once the reversed bar has been held at the kite for AirLoopHoldSeconds.
		Kite->SteerKite(-First);
		float SwitchSeconds = -1.0f;
		float SwitchClockDeg = 0.0f;
		for (Seconds = 0.0f; Seconds < 6.0f && OpenTurn(static_cast<int32>(-First)) < ReleaseAtDeg && !Kite->IsCrashed(); Seconds += EntryDeltaTime)
		{
			Kite->UpdateKite(EntryDeltaTime);
			if (SwitchSeconds < 0.0f && Kite->IsLooping() && Kite->GetLoopSide() == -First)
			{
				SwitchSeconds = Seconds + EntryDeltaTime;
				SwitchClockDeg = Kite->GetClockDeg();
			}
		}
		const float SecondHalfSeconds = Seconds;
		Standing.Fly(0.0f, 4.0f);

		FString Text;
		const TArray<FJumpLoop> Loops = NewJumpLoops(Kite, FirstIndex, Text);
		const TArray<FTrickLoop> Kinds = TrickRecognition::ClassifyLoops(Loops);
		const FString Which = FString::Printf(TEXT("First half %+.0f"), First);
		UE_LOG(LogKiteSurf, Log, TEXT("AirReverseMakesSLoop (%s): reversed after %.2f s at clock %+.1f; looping the other way %.2f s later (dead time %.2f s) at clock %+.1f; second half %.2f s; records:%s; classified '%s'"),
			*Which, FirstHalfSeconds, ReverseClockDeg, SwitchSeconds, DeadTimeSeconds, SwitchClockDeg, SecondHalfSeconds, *Text, *KindsOf(Kinds));

		TestTrue(FString::Printf(TEXT("%s: the reversed bar loops the kite the other way once held %.2f s at the kite (%.2f s, dead time %.2f s)"), *Which, Kite->AirLoopHoldSeconds, SwitchSeconds, DeadTimeSeconds),
			SwitchSeconds >= 0.0f && SwitchSeconds <= DeadTimeSeconds + Kite->AirLoopHoldSeconds + 2.0f * EntryDeltaTime);
		TestFalse(Which + TEXT(": the kite stays out of the water"), Kite->IsCrashed());
		if (!TestEqual(Which + TEXT(": two loop records"), Loops.Num(), 2))
		{
			continue;
		}
		for (int32 Half = 0; Half < 2; ++Half)
		{
			const FKiteLoopRecord& R = Loops[Half].Loop;
			const int32 Expected = static_cast<int32>(Half == 0 ? First : -First);
			const FString Part = FString::Printf(TEXT("%s, half %d"), *Which, Half + 1);
			TestEqual(Part + TEXT(": its direction"), R.Direction, Expected);
			TestTrue(FString::Printf(TEXT("%s: partial, 180 deg or more (%.0f deg, %s)"), *Part, R.TurnDeg, R.bCompleted ? TEXT("complete") : TEXT("part")),
				!R.bCompleted && !R.bKiteCrashed && R.TurnDeg >= 180.0f && R.TurnDeg < 360.0f);
		}
		const float GapSeconds = Loops[1].Loop.StartTimeSeconds - (Loops[0].Loop.StartTimeSeconds + Loops[0].Loop.DurationSeconds);
		TestTrue(FString::Printf(TEXT("%s: the second half starts as the first ends (gap %.2f s)"), *Which, GapSeconds), GapSeconds <= FLoopClassifySettings().SLoopMaxGapSeconds);
		TestEqual(Which + TEXT(": ClassifyLoops names an S-loop"), KindsOf(Kinds), FString(TEXT("SLoop")));
	}

	// The same in a real jump: the timed send and pop at 30 kn riding right, then in the air half a
	// loop one way and half the other. The jump's record names it.
	for (const float First : { 1.0f, -1.0f })
	{
		FEntryRideFixture Ride(30.0f, 1.0f);
		if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
		{
			return false;
		}
		const auto OpenTurnAtLeast = [](int32 Direction, float Deg)
		{
			return [Direction, Deg](const UKiteComponent* Kite)
			{
				FKiteLoopRecord Open;
				return Kite->GetLoopDirection() == Direction && Kite->GetOpenLoop(Open) && Open.TurnDeg >= Deg;
			};
		};
		const FAirBarResult Run = RunAirBar(Ride, {
			FAirBarPhase{ First, OpenTurnAtLeast(static_cast<int32>(First), ReverseAtDeg) },
			FAirBarPhase{ -First, OpenTurnAtLeast(static_cast<int32>(-First), ReleaseAtDeg) } }, 0.3f);
		FString Text;
		NewJumpLoops(Ride.Kite, Run.FirstRecordIndex, Text);
		const TArray<FTrickLoop> Kinds = TrickRecognition::ClassifyLoops(Run.Jump.Loops);
		const FString Which = FString::Printf(TEXT("In a jump, first half %+.0f"), First);
		UE_LOG(LogKiteSurf, Log, TEXT("AirReverseMakesSLoop (%s): %.1f m, %.2f s in the air, landed %d; kite records:%s; the jump's %d loops classify '%s', named '%s'"),
			*Which, Run.ApexCm / 100.0f, Run.AirtimeSeconds, Run.bLanded, *Text, Run.Jump.Loops.Num(), *KindsOf(Kinds), Run.bHasJump ? *Run.Jump.TrickName : TEXT("(none)"));
		TestTrue(Which + TEXT(": the jump was recorded"), Run.bHasJump);
		TestEqual(Which + TEXT(": the jump record's loops classify as an S-loop"), KindsOf(Kinds), FString(TEXT("SLoop")));
	}
	return true;
}

// On the water the loop entry is as it was: the bar loops the kite only once the kite is LoopClockDeg
// (35 deg) round on the bar's side; a full bar towards the other side flies it across first, and a
// full bar reversed mid-loop ends the loop and flies the kite back unless it is already 35 deg round
// on the new side. Checked on every step where a loop starts or changes side.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfKiteWaterLoopEntryUnchanged, "KiteSurf.Kite.WaterLoopEntryUnchanged", TrickLoopEntryTestsLocal::Flags)

bool FKiteSurfKiteWaterLoopEntryUnchanged::RunTest(const FString& Parameters)
{
	// Not at file scope: in a unity build it would reach the other test files.
	using namespace TrickLoopEntryTestsLocal;
	struct FCase { float ClockDeg; float Bar; float ReverseAfterSeconds; };
	// Wrong side, own side under 35 deg, own side past 35 deg, and past 35 deg reversed mid-loop.
	const FCase Cases[] = { { 40.0f, -1.0f, 0.0f }, { -40.0f, 1.0f, 0.0f }, { -20.0f, -1.0f, 0.0f }, { 45.0f, 1.0f, 0.0f }, { -45.0f, -1.0f, 0.8f }, { 45.0f, 1.0f, 0.8f } };
	for (const FCase& Case : Cases)
	{
		FEntryStandingFixture Standing;
		if (!TestNotNull(TEXT("Kite created"), Standing.Kite))
		{
			return false;
		}
		UKiteComponent* Kite = Standing.Kite;
		Standing.Park(Case.ClockDeg);
		const FString Which = FString::Printf(TEXT("On the water, parked at %+.0f, full bar %+.0f%s"), Case.ClockDeg, Case.Bar,
			Case.ReverseAfterSeconds > 0.0f ? TEXT(" then reversed") : TEXT(""));

		int32 Entries = 0;
		int32 BadEntries = 0;
		float FirstEntryClockDeg = 0.0f;
		float FirstEntrySeconds = -1.0f;
		float ReversedLoopingSeconds = 0.0f;
		float ReverseEntryClockDeg = 0.0f;
		bool bWasLooping = false;
		float WasSide = 0.0f;
		float Bar = Case.Bar;
		Kite->SteerKite(Bar);
		// Until half a second after the first loop starts (a held loop from the window edge in 15 kn
		// goes on into the water, as it always has), or 1.5 s after a reversal.
		const float TotalSeconds = Case.ReverseAfterSeconds > 0.0f ? Case.ReverseAfterSeconds + 1.5f : 8.0f;
		for (float Elapsed = 0.0f; Elapsed < TotalSeconds && !Kite->IsCrashed(); Elapsed += EntryDeltaTime)
		{
			if (Case.ReverseAfterSeconds <= 0.0f && FirstEntrySeconds >= 0.0f && Elapsed >= FirstEntrySeconds + 0.5f)
			{
				break;
			}
			if (Case.ReverseAfterSeconds > 0.0f && Bar == Case.Bar && Elapsed >= Case.ReverseAfterSeconds)
			{
				Bar = -Case.Bar;
				Kite->SteerKite(Bar);
			}
			Kite->UpdateKite(EntryDeltaTime);
			const bool bLooping = Kite->IsLooping();
			const float Side = Kite->GetLoopSide();
			if (bLooping && (!bWasLooping || Side != WasSide))
			{
				++Entries;
				const bool bAllowed = Kite->GetClockDeg() * Side >= Kite->LoopClockDeg - 1.0f;
				BadEntries += bAllowed ? 0 : 1;
				if (FirstEntrySeconds < 0.0f)
				{
					FirstEntrySeconds = Elapsed + EntryDeltaTime;
					FirstEntryClockDeg = Kite->GetClockDeg();
				}
				if (Side == -Case.Bar && Case.ReverseAfterSeconds > 0.0f)
				{
					ReverseEntryClockDeg = Kite->GetClockDeg();
				}
			}
			if (Case.ReverseAfterSeconds > 0.0f && Bar != Case.Bar && bLooping && Side == -Case.Bar)
			{
				ReversedLoopingSeconds += EntryDeltaTime;
			}
			bWasLooping = bLooping;
			WasSide = Side;
		}
		Kite->SteerKite(0.0f);
		UE_LOG(LogKiteSurf, Log, TEXT("WaterLoopEntryUnchanged: %s: %d loop entries (%d against the 35 deg rule), the first after %.2f s at clock %+.1f; looping the reversed way %.2f s (entered at %+.1f)"),
			*Which, Entries, BadEntries, FirstEntrySeconds, FirstEntryClockDeg, ReversedLoopingSeconds, ReverseEntryClockDeg);
		TestEqual(FString::Printf(TEXT("%s: every loop starts %.0f deg or more round on its side"), *Which, Kite->LoopClockDeg), BadEntries, 0);
		TestTrue(Which + TEXT(": the bar does loop the kite once it is round"), Entries >= 1);
		if (Case.ClockDeg * Case.Bar < Kite->LoopClockDeg)
		{
			TestTrue(FString::Printf(TEXT("%s: it flies across first (entered at %+.1f after %.2f s)"), *Which, FirstEntryClockDeg, FirstEntrySeconds),
				FirstEntrySeconds > Kite->GetSteeringDeadTimeSeconds() + 0.1f);
		}
		TestFalse(Which + TEXT(": the kite stays out of the water"), Kite->IsCrashed());
	}
	return true;
}

// A contra loop flown in the air, from a real jump: riding right (tack +1, along Up x Downwind) at 30
// kn, the timed send and pop, then in the air a full bar to the left (-1) for one loop. The kite
// loops left from overhead, its record says Direction -1 with RiderTravelSide +1, and
// TrickRecognition::IsContraLoop calls it contra; the same with the bar to the right is natural.
// Before the in-air entry neither loop could start from overhead: the kite had to be 35 deg round
// on the bar's side.
//
// Sign convention (TrickRecognition::IsContraLoop): Direction +1 is the kite turning to the rider's
// right (clockwise seen from the rider, the sign of UKiteComponent::GetTurnDeg); RiderTravelSide +1 is
// riding along Up x Downwind, to the right looking downwind. Riding right the right hand is the front
// hand, and pulling it turns the kite right, towards the travel: natural. A loop against the travel
// (Direction x RiderTravelSide < 0) is contra. Not yet confirmed against footage
// (docs/tricks.md section 9).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfKiteAirContraLoopIsContra, "KiteSurf.Kite.AirContraLoopIsContra", TrickLoopEntryTestsLocal::Flags)

bool FKiteSurfKiteAirContraLoopIsContra::RunTest(const FString& Parameters)
{
	// Not at file scope: in a unity build it would reach the other test files.
	using namespace TrickLoopEntryTestsLocal;
	for (const float Bar : { -1.0f, 1.0f })
	{
		FEntryRideFixture Ride(30.0f, 1.0f);
		if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
		{
			return false;
		}
		// Held until the first loop record completed after the bar went over.
		int32 PhaseFirstIndex = -1;
		const auto LoopDone = [&PhaseFirstIndex](const UKiteComponent* Kite)
		{
			if (PhaseFirstIndex < 0)
			{
				PhaseFirstIndex = Kite->GetLoopRecordCount();
			}
			int32 Direction = 0;
			return FirstNewCompleted(Kite, PhaseFirstIndex, Direction) != INDEX_NONE;
		};
		const FAirBarResult Run = RunAirBar(Ride, { FAirBarPhase{ Bar, LoopDone } });

		FString Text;
		const TArray<FJumpLoop> Loops = NewJumpLoops(Ride.Kite, Run.FirstRecordIndex, Text);
		const FKiteLoopRecord* Completed = nullptr;
		for (const FJumpLoop& L : Loops)
		{
			if (L.Loop.bCompleted)
			{
				Completed = &L.Loop;
				break;
			}
		}
		const FString Which = FString::Printf(TEXT("Riding right, full bar %+.0f in the air"), Bar);
		UE_LOG(LogKiteSurf, Log, TEXT("AirContraLoopIsContra: %s: took off %d, %.1f m, %.2f s in the air, landed %d; take-off at kite time %.2f s, landing %.2f s; records:%s; jump record '%s'"),
			*Which, Run.bTookOff, Run.ApexCm / 100.0f, Run.AirtimeSeconds, Run.bLanded, Run.TakeoffKiteSeconds, Run.LandingKiteSeconds, *Text,
			Run.bHasJump ? *Run.Jump.TrickName : TEXT("(none)"));

		TestTrue(Which + TEXT(": the rider jumped"), Run.bTookOff && Run.ApexCm > 300.0f);
		if (!TestNotNull(Which + TEXT(": a completed loop record"), Completed))
		{
			continue;
		}
		TestEqual(Which + TEXT(": the loop turns the bar's way"), Completed->Direction, static_cast<int32>(Bar));
		TestEqual(Which + TEXT(": the rider was travelling right"), Completed->RiderTravelSide, 1);
		TestEqual(Which + TEXT(": IsContraLoop"), TrickRecognition::IsContraLoop(*Completed), Bar < 0.0f);

		// The jump's own record has the loop (it overlaps the flight), and the classifier agrees.
		TestTrue(Which + TEXT(": the jump was recorded and landed"), Run.bHasJump && Run.bLanded);
		const TArray<FTrickLoop> Kinds = TrickRecognition::ClassifyLoops(Run.Jump.Loops);
		const FString Expected = Bar < 0.0f ? TEXT("Kiteloop(contra)") : TEXT("Kiteloop");
		TestEqual(FString::Printf(TEXT("%s: the jump record's loops classify as %s ('%s')"), *Which, *Expected, *Run.Jump.TrickName), KindsOf(Kinds), Expected);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
