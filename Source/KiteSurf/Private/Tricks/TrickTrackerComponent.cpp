#include "Tricks/TrickTrackerComponent.h"
#include "KiteSurfUnits.h"
#include "BoardMovementComponent.h"
#include "KiteComponent.h"
#include "Tricks/RiderAttitudeComponent.h"
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
}

void UTrickTrackerComponent::ClearSession()
{
	Session.Clear();
	LiveJump = FJumpRecord();
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
	if (!Attitude && GetOwner())
	{
		// The rider attitude (T1.2), found on the owner as the board and kite are.
		Attitude = GetOwner()->FindComponentByClass<URiderAttitudeComponent>();
	}

	// Everything below is the board's or the kite's own: the take-off counters and the apex time
	// from the board's BeginAirborne and air step, the landing facts from its landing, and the loop
	// records from the kite's per-step turn. The recorder reads the Last* fields only on the step
	// the jump count changes, when the board has just set them.
	FJumpRecorderInput In;
	In.BoardTimeSeconds = Board->GetSimTimeSeconds();
	In.BoardState = Board->GetBoardState();
	In.bCrashing = Board->IsCrashing();
	In.TakeoffCount = Board->GetTakeoffCount();
	In.JumpCount = Board->GetJumpCount();
	In.ResetCount = Board->GetResetCount();
	In.bLastTakeoffPopped = Board->WasLastTakeoffPopped();
	In.LastTakeoffTimeSeconds = Board->GetLastTakeoffTimeSeconds();
	In.Location = Board->UpdatedComponent ? Board->UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
	In.Velocity = Board->Velocity;
	In.LastApexCm = Board->GetLastJumpApexHeight();
	In.LastApexTimeSeconds = Board->GetCurrentJumpApexTimeSeconds();
	In.LastAirtimeSeconds = Board->GetLastJumpAirtime();
	In.LastDistanceCm = Board->GetLastJumpDistance();
	In.LastSinkRateCmS = KiteUnits::MToCm(Board->GetLastLandingSinkMS());
	In.LastLandingG = Board->GetLastLandingG();
	In.LastLandingAngleDeg = Board->GetLastLandingAngleDeg();
	In.bLastLandingClean = Board->WasLastLandingClean();
	In.LastLandingCause = Board->GetLastLandingVerdict().Cause;
	In.BoardForward = Board->GetBoardWorldQuat().GetAxisX();
	// The rider's rotation. The attitude stepped before the board, so on the touchdown step it still
	// holds the body as it met the water.
	In.bHasAttitude = Attitude != nullptr;
	if (Attitude)
	{
		In.bAttitudeActive = Attitude->IsSimulating();
		In.BodyQuat = Attitude->GetBodyQuat();
		In.AngularVelocityRadS = Attitude->GetAngularVelocity();
	}
	In.TensionN = Kite->GetLineTensionN();
	In.KiteElevationDeg = Kite->GetElevationDeg();
	In.KiteTimeSeconds = Kite->GetSimTimeSeconds();
	In.KiteLoops = &Kite->GetLoopRecords();
	In.bHasOpenLoop = Kite->GetOpenLoop(In.OpenLoop);

	FJumpRecord Finished;
	if (Session.Step(In, &Finished))
	{
		const FRotationRecognizer& Rotation = Session.GetRecorder().GetRotation();
		const float AboutUpDeg = FMath::RadiansToDegrees(static_cast<float>(Rotation.GetSpinAboutURad()));
		UE_LOG(LogKiteSurf, Log, TEXT("Trick tracker: jump %d %s, %s (cause %s), %.1f pts; rotation: %d inversion(s), spin %.0f deg (%d half turns; about up %.0f, flight turned %.0f, sigma %+.0f), heading %.0f deg, landed %s"),
			Finished.Index, *Finished.TrickName, *UEnum::GetValueAsString(Finished.Grade), *UEnum::GetValueAsString(Finished.LandingCause),
			Finished.Score.Total * Finished.RepeatFactor, Finished.Inversions.Num(), Finished.SpinDeg, Finished.SpinHalfTurns,
			AboutUpDeg, AboutUpDeg - Finished.SpinDeg,
			Rotation.GetFrame().Sigma, Finished.NetHeadingDeg, *UEnum::GetValueAsString(Finished.LandingStance));
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

	// The kite's clock and the board's advance by the same fixed step.
	const float KiteMinusBoard = In.KiteTimeSeconds - In.BoardTimeSeconds;
	const float TakeoffKite = LiveJump.TakeoffTimeSeconds + KiteMinusBoard;
	const float ApexKite = LiveJump.ApexTimeSeconds + KiteMinusBoard;
	auto AddLoop = [this, TakeoffKite, ApexKite](const FKiteLoopRecord& Loop)
	{
		FJumpLoop& JumpLoop = LiveJump.Loops.AddDefaulted_GetRef();
		JumpLoop.Loop = Loop;
		JumpLoop.StartSinceTakeoffSeconds = Loop.StartTimeSeconds - TakeoffKite;
		JumpLoop.StartSinceApexSeconds = Loop.StartTimeSeconds - ApexKite;
		JumpLoop.RiderHeightAtStartCm = Loop.RiderZAtStartCm - static_cast<float>(LiveJump.TakeoffLocation.Z);
	};
	for (const FKiteLoopRecord& Loop : Kite->GetLoopRecords())
	{
		if (Loop.StartTimeSeconds + Loop.DurationSeconds >= TakeoffKite)
		{
			AddLoop(Loop);
		}
	}
	if (In.bHasOpenLoop && In.OpenLoop.TurnDeg >= Recorder.Settings.MinOpenLoopDeg)
	{
		AddLoop(In.OpenLoop);
	}
}
