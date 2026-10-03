#include "Tricks/TrickTrackerComponent.h"
#include "Tricks/LandingMath.h"
#include "BoardMovementComponent.h"
#include "KiteComponent.h"
#include "UI/KiteSurfGameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "KiteSurf.h"

UTrickTrackerComponent::UTrickTrackerComponent()
{
	// Stepped by the pawn after the board, at the fixed simulation step.
	PrimaryComponentTick.bCanEverTick = false;
}

void UTrickTrackerComponent::SetSources(UBoardMovementComponent* InBoard, UKiteComponent* InKite)
{
	Board = InBoard;
	Kite = InKite;
	bHasPrevious = false;
	bHasHeading = false;
}

void UTrickTrackerComponent::ClearSession()
{
	Session.Clear();
	LiveJump = FJumpRecord();
}

float UTrickTrackerComponent::SignedHeadingTurnDeg(const FVector& PreviousHeading, const FVector& Heading, const FVector& LineDir)
{
	const FVector Axis = LineDir.GetSafeNormal();
	if (Axis.IsNearlyZero())
	{
		return 0.0f;
	}
	const FVector From = FVector::VectorPlaneProject(PreviousHeading, Axis).GetSafeNormal();
	const FVector To = FVector::VectorPlaneProject(Heading, Axis).GetSafeNormal();
	if (From.IsNearlyZero() || To.IsNearlyZero())
	{
		return 0.0f;
	}
	// The kite's right is Nose x LineDir, so a turn to the right is a rotation about -LineDir.
	const double Sin = FVector::DotProduct(FVector::CrossProduct(From, To), -Axis);
	const double Cos = FVector::DotProduct(From, To);
	return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Sin, Cos)));
}

void UTrickTrackerComponent::StepLoops()
{
	const FVector RiderLocation = Board->UpdatedComponent ? Board->UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
	const FVector Heading = Kite->GetKiteHeading();
	const FVector LineDir = Kite->GetKiteWorldPosition() - RiderLocation;

	// A reset places the board and the kite again: whatever the kite was doing is not a loop.
	const int32 ResetCount = Board->GetResetCount();
	const bool bReset = ResetCount != SeenResetCount;
	SeenResetCount = ResetCount;

	float TurnDeg = 0.0f;
	if (bHasHeading && !bReset)
	{
		TurnDeg = SignedHeadingTurnDeg(PreviousHeading, Heading, LineDir);
	}
	if (bReset || FMath::Abs(TurnDeg) > MaxStepTurnDeg)
	{
		// Placed or relaunched, not turned.
		LoopTracker.CancelRun();
		TurnDeg = 0.0f;
	}
	PreviousHeading = Heading;
	bHasHeading = true;
	LastStepTurnDeg = TurnDeg;

	FKiteLoopSample Sample;
	Sample.TimeSeconds = Kite->GetSimTimeSeconds();
	Sample.TurnDeg = Kite->IsCrashed() ? 0.0f : TurnDeg;
	Sample.ElevationDeg = Kite->GetElevationDeg();
	Sample.TensionN = Kite->GetLineTensionN();
	Sample.RiderZCm = static_cast<float>(RiderLocation.Z);
	Sample.RiderVelocity = Board->Velocity;
	Sample.DownwindDir = Kite->GetDownwindDir();
	Sample.bFlying = Kite->AreLinesTaut();
	Sample.bCrashed = Kite->IsCrashed();
	LoopTracker.Step(Sample);
}

void UTrickTrackerComponent::StepTracker(float StepSeconds)
{
	if ((!Board || !Kite) && GetOwner())
	{
		// Sources not set (a pawn built some other way): follow the owner's board and kite.
		SetSources(GetOwner()->FindComponentByClass<UBoardMovementComponent>(), GetOwner()->FindComponentByClass<UKiteComponent>());
	}
	if (!Board || !Kite)
	{
		return;
	}

	StepLoops();

	const EBoardState State = Board->GetBoardState();
	const bool bAirborne = State == EBoardState::Airborne;
	const float BoardTime = Board->GetSimTimeSeconds();
	const float KiteTime = Kite->GetSimTimeSeconds();

	// Take-off: the board has just entered the air. Not on the first step, whose state may be left over.
	if (bHasPrevious && bAirborne && !bWasAirborne)
	{
		++TakeoffCount;
		const float AirtimeSoFar = FMath::Max(Board->GetCurrentJumpAirtime(), 0.0f);
		LastTakeoffTimeSeconds = BoardTime - AirtimeSoFar;
		TakeoffKiteTimeSeconds = KiteTime - AirtimeSoFar;
		// A pop leaves the water before the board's step and has this step's airtime already; a
		// kite lift-off starts inside the step with none (see the class comment).
		bLastTakeoffPopped = AirtimeSoFar > 0.5f * FMath::Max(StepSeconds, KINDA_SMALL_NUMBER);
		LastAirborneSinkCmS = 0.0f;
	}

	// Landing or crash: the board has counted a jump. The sink is the last airborne step's.
	const int32 JumpCount = Board->GetJumpCount();
	if (JumpCount != LastJumpCount)
	{
		LastSinkCmS = LastAirborneSinkCmS;
		LastLandingG = LandingMath::ComputeLandingG(LastSinkCmS, LandingAbsorbDistanceCm);
		LastJumpCount = JumpCount;
	}
	if (bAirborne)
	{
		LastAirborneSinkCmS = FMath::Max(-static_cast<float>(Board->Velocity.Z), 0.0f);
	}
	bWasAirborne = bAirborne;
	bHasPrevious = true;

	FJumpRecorderInput In;
	In.BoardTimeSeconds = BoardTime;
	In.BoardState = State;
	In.bCrashing = Board->IsCrashing();
	In.TakeoffCount = TakeoffCount;
	In.JumpCount = JumpCount;
	In.ResetCount = Board->GetResetCount();
	In.bLastTakeoffPopped = bLastTakeoffPopped;
	In.LastTakeoffTimeSeconds = LastTakeoffTimeSeconds;
	In.Location = Board->UpdatedComponent ? Board->UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
	In.Velocity = Board->Velocity;
	In.LastApexCm = Board->GetLastJumpApexHeight();
	In.LastApexTimeSeconds = -1.0f; // the recorder takes the step it saw the board highest
	In.LastAirtimeSeconds = Board->GetLastJumpAirtime();
	In.LastDistanceCm = Board->GetLastJumpDistance();
	In.LastSinkRateCmS = LastSinkCmS;
	In.LastLandingG = LastLandingG;
	In.LastLandingAngleDeg = 0.0f; // not exposed by the board yet
	In.bLastLandingClean = Board->WasLastLandingClean();
	In.TensionN = Kite->GetLineTensionN();
	In.KiteElevationDeg = Kite->GetElevationDeg();
	In.KiteTimeSeconds = KiteTime;
	In.KiteLoops = &LoopTracker.GetRecords();
	In.bHasOpenLoop = LoopTracker.GetOpenRun(In.OpenLoop);

	FJumpRecord Finished;
	if (Session.Step(In, &Finished))
	{
		UE_LOG(LogKiteSurf, Log, TEXT("Trick tracker: jump %d %s, %s, %.1f pts"), Finished.Index, *Finished.TrickName,
			*UEnum::GetValueAsString(Finished.Grade), Finished.Score.Total * Finished.RepeatFactor);
		if (UWorld* World = GetWorld())
		{
			if (UKiteSurfGameInstance* GameInstance = World->GetGameInstance<UKiteSurfGameInstance>())
			{
				GameInstance->RecordTrickLanding(Finished);
			}
		}
	}

	BuildLiveJump(In);
}

void UTrickTrackerComponent::BuildLiveJump(const FJumpRecorderInput& In)
{
	const FJumpRecorder& Recorder = Session.GetRecorder();
	if (!Recorder.IsJumpOpen())
	{
		LiveJump = FJumpRecord();
		return;
	}
	LiveJump = Recorder.GetLive();
	LiveJump.Loops.Reset();

	const float ApexKite = TakeoffKiteTimeSeconds + (LiveJump.ApexTimeSeconds - LiveJump.TakeoffTimeSeconds);
	auto AddLoop = [this, ApexKite](const FKiteLoopRecord& Loop)
	{
		FJumpLoop& JumpLoop = LiveJump.Loops.AddDefaulted_GetRef();
		JumpLoop.Loop = Loop;
		JumpLoop.StartSinceTakeoffSeconds = Loop.StartTimeSeconds - TakeoffKiteTimeSeconds;
		JumpLoop.StartSinceApexSeconds = Loop.StartTimeSeconds - ApexKite;
		JumpLoop.RiderHeightAtStartCm = Loop.RiderZAtStartCm - static_cast<float>(LiveJump.TakeoffLocation.Z);
	};
	for (const FKiteLoopRecord& Loop : LoopTracker.GetRecords())
	{
		if (Loop.StartTimeSeconds + Loop.DurationSeconds >= TakeoffKiteTimeSeconds)
		{
			AddLoop(Loop);
		}
	}
	if (In.bHasOpenLoop && In.OpenLoop.TurnDeg >= Recorder.Settings.MinOpenLoopDeg)
	{
		AddLoop(In.OpenLoop);
	}
}
