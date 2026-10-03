#include "KiteSurfHUD.h"
#include "KiteSurfUnits.h"
#include "KiteRiderPawn.h"
#include "BoardMovementComponent.h"
#include "KiteComponent.h"
#include "Tricks/TrickTrackerComponent.h"
#include "Tricks/TrickRecognition.h"
#include "Tricks/TrickNaming.h"
#include "Tricks/SessionScoring.h"
#include "Tricks/TrickSessionSubsystem.h"
#include "Tricks/FreestyleHeatSubsystem.h"
#include "WindComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"
#include "UI/KiteSurfPauseMenuWidget.h"
#include "UI/KiteSurfGameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerState.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "School/LessonDirector.h"
#include "School/LessonSubsystem.h"
#include "School/SchoolOnboarding.h"

AKiteSurfHUD::AKiteSurfHUD()
{
}

void AKiteSurfHUD::TogglePauseMenu()
{
	// On a lesson's result card the pause button is "Lesson menu": the School menu takes it when it is
	// there (S5); otherwise the pause menu opens as usual.
	if (!ActivePauseMenuWidget && HandleLessonAction(ELessonHUDAction::Menu))
	{
		return;
	}
	UWorld* World = GetWorld();
	const bool bHasViewport = World && World->GetGameViewport() != nullptr;
	if (ActivePauseMenuWidget && (!bHasViewport || ActivePauseMenuWidget->IsInViewport()))
	{
		HidePauseMenu();
	}
	else
	{
		ShowPauseMenu();
	}
}

void AKiteSurfHUD::ShowPauseMenu()
{
	UWorld* World = GetWorld();
	APlayerController* PC = GetOwningPlayerController();
	if (!World || !PC)
	{
		return;
	}
	TSubclassOf<UKiteSurfPauseMenuWidget> ClassToSpawn = PauseMenuWidgetClass ? PauseMenuWidgetClass : TSubclassOf<UKiteSurfPauseMenuWidget>(UKiteSurfPauseMenuWidget::StaticClass());
	ActivePauseMenuWidget = PC->IsLocalPlayerController()
		? CreateWidget<UKiteSurfPauseMenuWidget>(PC, ClassToSpawn)
		: CreateWidget<UKiteSurfPauseMenuWidget>(World, ClassToSpawn);
	if (!ActivePauseMenuWidget)
	{
		return;
	}
	if (World->GetGameViewport() != nullptr)
	{
		ActivePauseMenuWidget->AddToViewport(100);
	}
	if (!UGameplayStatics::SetGamePaused(World, true))
	{
		if (AWorldSettings* WS = World->GetWorldSettings())
		{
			if (!PC->PlayerState)
			{
				APlayerState* PS = World->SpawnActor<APlayerState>();
				PC->SetPlayerState(PS);
			}
			if (PC->PlayerState)
			{
				WS->SetPauserPlayerState(PC->PlayerState);
			}
		}
	}
	PC->bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(ActivePauseMenuWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(InputMode);
	ActivePauseMenuWidget->FocusFirst();
}

void AKiteSurfHUD::HidePauseMenu()
{
	if (ActivePauseMenuWidget)
	{
		UKiteSurfPauseMenuWidget* WidgetToClose = ActivePauseMenuWidget;
		ActivePauseMenuWidget = nullptr;
		WidgetToClose->OnResumeClicked();
	}
}

float AKiteSurfHUD::CmPerSecToKnots(float SpeedCmPerSec)
{
	return KiteUnits::CmSToKnots(SpeedCmPerSec);
}

float AKiteSurfHUD::KnotsToCmPerSec(float Knots)
{
	return KiteUnits::KnotsToCmS(Knots);
}

FString AKiteSurfHUD::FormatJumpLive(float HeightCm, float DistanceCm, float AirSeconds)
{
	return FString::Printf(TEXT("%.1f m high   %.0f m far   %.1f s"), KiteUnits::CmToM(HeightCm), KiteUnits::CmToM(DistanceCm), AirSeconds);
}

FString AKiteSurfHUD::FormatJumpResult(float ApexCm, float DistanceCm, float AirSeconds)
{
	return FString::Printf(TEXT("JUMP  %.1f m high   %.0f m far   %.1f s"), KiteUnits::CmToM(ApexCm), KiteUnits::CmToM(DistanceCm), AirSeconds);
}

void AKiteSurfHUD::UpdateJumpReadout(const UBoardMovementComponent* Board, float DeltaTime)
{
	// A hop off a wave is not a jump worth announcing.
	const float MinHeightCm = 100.0f;
	if (!Board)
	{
		JumpReadoutText.Reset();
		return;
	}

	if (Board->GetJumpCount() != SeenJumpCount)
	{
		// A jump has just finished: show what it came to.
		SeenJumpCount = Board->GetJumpCount();
		const bool bLandedClean = Board->WasLastLandingClean();
		if (Board->GetLastJumpApexHeight() >= MinHeightCm)
		{
			JumpReadoutText = FormatJumpResult(Board->GetLastJumpApexHeight(), Board->GetLastJumpDistance(), Board->GetLastJumpAirtime());
			JumpResultRemainingTime = 4.0f;
			bJumpReadoutNewBest = IsNewBestJump(Board->GetLastJumpApexHeight(), BestHeightBeforeJumpCm, bLandedClean);
			bJumpReadoutLive = false;
		}
		// The best counts landed jumps only: a crash is not a height the rider can claim.
		if (bLandedClean)
		{
			BestHeightBeforeJumpCm = FMath::Max(BestHeightBeforeJumpCm, Board->GetLastJumpApexHeight());
		}
		return;
	}

	if (Board->GetBoardState() == EBoardState::Airborne && Board->GetCurrentJumpHeight() >= MinHeightCm)
	{
		JumpReadoutText = FormatJumpLive(Board->GetCurrentJumpHeight(), Board->GetCurrentJumpDistance(), Board->GetCurrentJumpAirtime());
		JumpResultRemainingTime = 0.0f;
		bJumpReadoutNewBest = BestHeightBeforeJumpCm > 0.0f && Board->GetCurrentJumpHeight() > BestHeightBeforeJumpCm;
		bJumpReadoutLive = true;
		return;
	}

	if (bJumpReadoutLive)
	{
		// Came down below a metre without the jump being counted yet: clear the live figures.
		bJumpReadoutLive = false;
		JumpReadoutText.Reset();
	}
	if (JumpResultRemainingTime > 0.0f)
	{
		JumpResultRemainingTime = FMath::Max(0.0f, JumpResultRemainingTime - DeltaTime);
		if (JumpResultRemainingTime <= 0.0f)
		{
			JumpReadoutText.Reset();
			bJumpReadoutNewBest = false;
		}
	}
}

FString AKiteSurfHUD::GradeText(ELandingGrade Grade)
{
	switch (Grade)
	{
	case ELandingGrade::Stomped:
		return TEXT("STOMPED");
	case ELandingGrade::Clean:
		return TEXT("CLEAN");
	case ELandingGrade::Sketchy:
		return TEXT("SKETCHY");
	case ELandingGrade::Crash:
	default:
		return TEXT("CRASH");
	}
}

FLinearColor AKiteSurfHUD::GradeColor(ELandingGrade Grade)
{
	switch (Grade)
	{
	case ELandingGrade::Stomped:
		return FLinearColor(0.35f, 1.0f, 0.45f);
	case ELandingGrade::Clean:
		return FLinearColor::White;
	case ELandingGrade::Sketchy:
		return FLinearColor(1.0f, 0.7f, 0.2f);
	case ELandingGrade::Crash:
	default:
		return FLinearColor(1.0f, 0.35f, 0.35f);
	}
}

FString AKiteSurfHUD::FormatJumpCard(const FJumpRecord& Record)
{
	const float Paid = Record.Score.Total * Record.RepeatFactor;
	FString Card = FString::Printf(TEXT("%s  %s  %d pts"), *Record.TrickName, *GradeText(Record.Grade), FMath::RoundToInt(Paid));
	if (Record.RepeatFactor < 0.999f && Record.Grade != ELandingGrade::Crash)
	{
		Card += FString::Printf(TEXT("  (repeat %d%%)"), FMath::RoundToInt(100.0f * Record.RepeatFactor));
	}
	Card += FString::Printf(TEXT("\n%.1f g landing"), Record.LandingG);
	// T2.6: one line, the cause the board's verdict picked. The grade is the same verdict's, which names
	// a cause only for a sketchy landing or a crash, so the two agree.
	const FString Cause = LandingCauseLine(Record.LandingCause);
	if (!Cause.IsEmpty())
	{
		Card += TEXT("\n") + Cause;
	}
	return Card;
}

FString AKiteSurfHUD::LandingCauseLine(ELandingCause Cause)
{
	switch (Cause)
	{
	case ELandingCause::UnderRotated:    return TEXT("Under-rotated: commit the roll earlier");
	case ELandingCause::OverRotated:     return TEXT("Over-rotated: stop the turn sooner");
	case ELandingCause::Sideways:        return TEXT("Board sideways at touchdown");
	case ELandingCause::Inverted:        return TEXT("Upside down at touchdown");
	case ELandingCause::KiteTooLow:      return TEXT("Kite too low at touchdown");
	case ELandingCause::TooHard:         return TEXT("Landed too hard: redirect the kite");
	case ELandingCause::BoardOff:        return TEXT("Board not caught");
	case ELandingCause::BarLost:         return TEXT("Bar lost");
	case ELandingCause::PassUnfinished:  return TEXT("Pass not finished");
	case ELandingCause::BoardNotAligned: return TEXT("Board not lined up with the feet");
	case ELandingCause::FootOutOfStrap:  return TEXT("Back foot still out of the strap");
	case ELandingCause::FootLate:        return TEXT("Back foot back in too late");
	case ELandingCause::BoardCaughtLate: return TEXT("Board caught late: let go sooner");
	case ELandingCause::None:
	default:                             return FString();
	}
}

FString AKiteSurfHUD::GetJumpCardCauseText() const
{
	TArray<FString> Lines;
	JumpCardText.ParseIntoArray(Lines, TEXT("\n"), false);
	return Lines.Num() >= 3 ? Lines[2] : FString();
}

namespace
{
	/** The signature has something to name: a loop, an inversion, a spin, a grab, a pass... */
	bool HasTrickElement(const FTrickSignature& Signature)
	{
		return Signature.Loops.Num() > 0 || Signature.Inversions.Num() > 0 || Signature.SpinHalfTurns != 0 || Signature.bRaley
			|| Signature.bSBend || Signature.Grabs.Num() > 0 || Signature.bOneFooter || Signature.BoardOff != ETrickBoardOff::None
			|| Signature.Passes.Num() > 0;
	}
}

FString AKiteSurfHUD::FormatTrickTicker(const FJumpRecord& LiveJump)
{
	const FTrickSignature Signature = TrickRecognition::SignatureFromJump(LiveJump);
	return HasTrickElement(Signature) ? TrickNaming::Name(Signature) : FString();
}

float AKiteSurfHUD::GetLiveRotationDegrees(const UTrickTrackerComponent* Tracker)
{
	if (!Tracker || !Tracker->IsJumpInProgress())
	{
		return 0.0f;
	}
	const FRotationRecognizer& Rotation = Tracker->GetJumpSession().GetRecorder().GetRotation();
	if (!Rotation.HasBegun())
	{
		return 0.0f;
	}
	return FMath::RadiansToDegrees(Rotation.GetBodyAxisRad().Size());
}

FString AKiteSurfHUD::FormatTickerDegrees(float DegreesTurned)
{
	// Below this the rider has barely left upright: not worth a number yet.
	const float MinDegreesShown = 10.0f;
	if (DegreesTurned < MinDegreesShown)
	{
		return FString();
	}
	return FString::Printf(TEXT("%.0f°"), DegreesTurned);
}

void AKiteSurfHUD::ShowJumpCard(const FJumpRecord& Record, bool bIsNewTrick)
{
	JumpCardText = FormatJumpCard(Record);
	JumpCardGrade = Record.Grade;
	JumpCardRemainingTime = 4.0f;
	TickerText.Reset();
	TickerDegreesText.Reset();
	bJumpCardIsNewTrick = bIsNewTrick;
	NewTrickRecordName = Record.TrickName;
}

void AKiteSurfHUD::UpdateJumpCard(const UTrickTrackerComponent* Tracker, float DeltaTime)
{
	// The same threshold as the jump readout: a hop is not a trick.
	const float MinHeightCm = 100.0f;
	if (!Tracker)
	{
		JumpCardText.Reset();
		TickerText.Reset();
		TickerDegreesText.Reset();
		JumpCardRemainingTime = 0.0f;
		bJumpCardIsNewTrick = false;
		return;
	}

	if (Tracker->GetJumpRecordCount() != SeenRecordCount)
	{
		SeenRecordCount = Tracker->GetJumpRecordCount();
		FJumpRecord Record;
		if (Tracker->GetLastJumpRecord(Record) && Record.ApexHeightCm >= MinHeightCm)
		{
			ShowJumpCard(Record, Tracker->WasLastLandingNewTrick());
			return;
		}
	}

	if (Tracker->IsJumpInProgress())
	{
		TickerText = FormatTrickTicker(Tracker->GetLiveJump());
		// The degrees line rides beside the name, e.g. "Back roll" + "240°" (review batch E); never
		// shown with no name, so a grab or loop never picks up a stray number from drift in the
		// body-axis integral.
		TickerDegreesText = TickerText.IsEmpty() ? FString() : FormatTickerDegrees(GetLiveRotationDegrees(Tracker));
	}
	else
	{
		TickerText.Reset();
		TickerDegreesText.Reset();
	}

	if (JumpCardRemainingTime > 0.0f)
	{
		JumpCardRemainingTime = FMath::Max(0.0f, JumpCardRemainingTime - DeltaTime);
		if (JumpCardRemainingTime <= 0.0f)
		{
			JumpCardText.Reset();
			bJumpCardIsNewTrick = false;
		}
	}
}

FString AKiteSurfHUD::FormatKnots(float SpeedCmPerSec, bool bIncludeUnit)
{
	float Knots = CmPerSecToKnots(FMath::Abs(SpeedCmPerSec));
	if (bIncludeUnit)
	{
		return FString::Printf(TEXT("%.1f kn"), Knots);
	}
	return FString::Printf(TEXT("%.1f"), Knots);
}

void AKiteSurfHUD::ShowJumpRejection(EJumpRejectReason Reason)
{
	JumpRejectionText = UBoardMovementComponent::JumpRejectReasonToString(Reason);
	if (!JumpRejectionText.IsEmpty())
	{
		JumpRejectionRemainingTime = 1.5f;
	}
	else
	{
		JumpRejectionRemainingTime = 0.0f;
	}
}

FString AKiteSurfHUD::FormatLandingCard(float LandingG, bool bHot, bool bClean)
{
	return FString::Printf(TEXT("%s %.1f g%s"), bClean ? TEXT("LANDED") : TEXT("CRASH"), LandingG, bHot ? TEXT("  HOT") : TEXT(""));
}

void AKiteSurfHUD::UpdateLandingCard(const UBoardMovementComponent* Board, float DeltaTime)
{
	LandingCardRemainingTime = FMath::Max(0.0f, LandingCardRemainingTime - FMath::Max(DeltaTime, 0.0f));
	if (!Board)
	{
		return;
	}
	const int32 Count = Board->GetLandingCount();
	// The first look only learns the count: a landing from before the HUD saw the board is not news.
	if (SeenLandingCount >= 0 && Count != SeenLandingCount)
	{
		bLandingCardHot = Board->WasLastLandingHot();
		bLandingCardClean = Board->WasLastLandingClean();
		LandingCardText = FormatLandingCard(Board->GetLastLandingG(), bLandingCardHot, bLandingCardClean);
		LandingCardRemainingTime = LandingCardSeconds;
	}
	SeenLandingCount = Count;
}

void AKiteSurfHUD::DrawLandingCard(float ScreenW, float ScreenH)
{
	if (LandingCardRemainingTime <= 0.0f || LandingCardText.IsEmpty())
	{
		return;
	}
	// Above the jump-rejection line, centred: green for a clean landing, red for a crash, and the
	// whole card orange when it was hot. It fades over its last half second.
	const float Alpha = FMath::Clamp(LandingCardRemainingTime / 0.5f, 0.0f, 1.0f);
	const float Scale = 1.6f;
	float TextW = 0.0f;
	float TextH = 0.0f;
	GetTextSize(LandingCardText, TextW, TextH, nullptr, Scale);
	const float TextX = ScreenW * 0.5f - TextW * 0.5f;
	const float TextY = (ScreenH - 50.0f) - 235.0f;
	const FLinearColor Panel = bLandingCardHot ? FLinearColor(0.25f, 0.1f, 0.02f, 0.75f * Alpha) : FLinearColor(0.02f, 0.05f, 0.1f, 0.75f * Alpha);
	const FLinearColor Ink = !bLandingCardClean ? FLinearColor(1.0f, 0.35f, 0.35f, Alpha) : (bLandingCardHot ? FLinearColor(1.0f, 0.6f, 0.2f, Alpha) : FLinearColor(0.5f, 1.0f, 0.6f, Alpha));
	DrawRect(Panel, TextX - 16.0f, TextY - 6.0f, TextW + 32.0f, TextH + 12.0f);
	DrawText(LandingCardText, Ink, TextX, TextY, nullptr, Scale);
}

FString AKiteSurfHUD::FormatRotateCue(bool bRotateHeld)
{
	return bRotateHeld ? FString(TEXT("ROTATE")) : FString();
}

float AKiteSurfHUD::ComputePreWindMeterFraction(float PreWindAmount, bool bLoading, bool bRotateHeld)
{
	if (!bLoading || !bRotateHeld)
	{
		return 0.0f;
	}
	return FMath::Clamp(PreWindAmount, 0.0f, 1.0f);
}

void AKiteSurfHUD::UpdateRotateHUD(const AKiteRiderPawn* RiderPawn)
{
	if (!RiderPawn)
	{
		RotateCueText.Reset();
		PreWindMeterFraction = 0.0f;
		return;
	}
	const UBoardMovementComponent* Board = RiderPawn->GetBoardMovement();
	const bool bRotateHeld = RiderPawn->IsRotateHeld();
	RotateCueText = FormatRotateCue(bRotateHeld);
	PreWindMeterFraction = ComputePreWindMeterFraction(RiderPawn->GetPreWindAmount(), Board && Board->IsLoadHeld(), bRotateHeld);
}

void AKiteSurfHUD::DrawRotateCue(float ScreenX, float ScreenY)
{
	if (!RotateCueText.IsEmpty())
	{
		DrawText(RotateCueText, FLinearColor(0.55f, 0.9f, 1.0f), ScreenX, ScreenY, nullptr, 1.1f);
	}
	if (PreWindMeterFraction > 0.0f)
	{
		const float MeterW = 160.0f;
		const float MeterH = 10.0f;
		const float MeterY = ScreenY + 20.0f;
		DrawRect(FLinearColor(0.08f, 0.1f, 0.12f, 0.6f), ScreenX, MeterY, MeterW, MeterH);
		DrawRect(FLinearColor(0.55f, 0.9f, 1.0f, 0.9f), ScreenX, MeterY, MeterW * PreWindMeterFraction, MeterH);
	}
}

void AKiteSurfHUD::ShowNotice(const FString& Text)
{
	JumpRejectionText = Text;
	JumpRejectionRemainingTime = Text.IsEmpty() ? 0.0f : 2.5f;
}

void AKiteSurfHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	const float ScreenW = Canvas->ClipX;
	const float ScreenH = Canvas->ClipY;
	const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f;

	DrawFPS(ScreenW - 130.0f, 25.0f);

	// The kite school's lesson layer, while a lesson runs.
	UpdateLessonLayer(DeltaTime);
	// The first-run tutorial's line (lessons A1 to A3, S7).
	UpdateTutorialHint();
	const bool bLesson = LessonLayer.IsVisible();

	AKiteRiderPawn* RiderPawn = Cast<AKiteRiderPawn>(GetOwningPawn());
	if (RiderPawn)
	{
		BindLessonInput(RiderPawn);
		DrawTelemetry(RiderPawn);
		// Under the telemetry: the lesson's speed band, on a speed-band step.
		LessonLayer.DrawSpeedBand(*this, 20.0f, 252.0f, 330.0f);
		// Top right, under the frame rate: which way the wind blows across the view.
		DrawWindFlag(RiderPawn, ScreenW - 170.0f, 64.0f, 150.0f);
		// Bottom left: the rider is in the bottom centre of the view.
		DrawWindWindowArc(RiderPawn, 180.0f, ScreenH - 50.0f, 110.0f);
		DrawPowerGauge(RiderPawn, ScreenW - 200.0f, ScreenH - 250.0f, 40.0f, 200.0f);
		// Left of the power gauge: what the hands are doing to the bar.
		DrawControlBar(RiderPawn, ScreenW - 500.0f, ScreenH - 280.0f, 230.0f, 245.0f);
		// Above the control bar: the rotate modifier cue and, during a modified load, its pre-wind meter
		// (review batch E, "show and teach the rotation").
		UpdateRotateHUD(RiderPawn);
		DrawRotateCue(ScreenW - 500.0f, ScreenH - 312.0f);
		UpdateLandingCard(RiderPawn->GetBoardMovement(), DeltaTime);
		DrawLandingCard(ScreenW, ScreenH);
	}

	// The lesson panel at the top centre; the jump readout and the trick card move down under it.
	float ReadoutTop = 70.0f;
	if (bLesson)
	{
		const float Margin = FMath::Max(16.0f, ScreenH * 0.02f);
		const float PanelBottom = LessonLayer.DrawPanel(*this, ScreenW, ScreenH, Margin);
		if (PanelBottom > Margin)
		{
			ReadoutTop = PanelBottom + 12.0f;
		}
		// Under the panel (or at the top over a result card): the tutorial's welcome, skip and "complete" lines.
		const float HintBottom = DrawTutorialHint(ScreenW, ScreenH, PanelBottom > Margin ? PanelBottom + 8.0f : Margin);
		ReadoutTop = FMath::Max(ReadoutTop, HintBottom + 12.0f);
	}

	// Top centre: how high and how far the jump is going, then what it came to.
	UpdateJumpReadout(RiderPawn ? RiderPawn->GetBoardMovement() : nullptr, DeltaTime);
	UpdateJumpCard(RiderPawn ? RiderPawn->GetTrickTracker() : nullptr, DeltaTime);
	float BelowReadoutY = ReadoutTop;
	if (!JumpReadoutText.IsEmpty())
	{
		const float Scale = bJumpReadoutLive ? 1.9f : 1.6f;
		float TextW = 0.0f;
		float TextH = 0.0f;
		GetTextSize(JumpReadoutText, TextW, TextH, nullptr, Scale);
		const float TextX = ScreenW * 0.5f - TextW * 0.5f;
		const float TextY = ReadoutTop;
		DrawRect(FLinearColor(0.02f, 0.05f, 0.1f, 0.7f), TextX - 16.0f, TextY - 6.0f, TextW + 32.0f, TextH + 12.0f);
		DrawText(JumpReadoutText, bJumpReadoutNewBest ? FLinearColor(1.0f, 0.85f, 0.2f) : FLinearColor::White, TextX, TextY, nullptr, Scale);
		BelowReadoutY = TextY + TextH + 16.0f;
	}

	// Under the readout: the trick being flown while in the air, then the finished jump's card. The
	// degrees turned so far ride beside the name (review batch E), e.g. "Back roll 240°".
	if (!TickerText.IsEmpty())
	{
		const FString TickerLine = TickerDegreesText.IsEmpty() ? TickerText : FString::Printf(TEXT("%s  %s"), *TickerText, *TickerDegreesText);
		float TextW = 0.0f;
		float TextH = 0.0f;
		GetTextSize(TickerLine, TextW, TextH, nullptr, 1.5f);
		const float TextX = ScreenW * 0.5f - TextW * 0.5f;
		DrawRect(FLinearColor(0.02f, 0.05f, 0.1f, 0.7f), TextX - 14.0f, BelowReadoutY - 5.0f, TextW + 28.0f, TextH + 10.0f);
		DrawText(TickerLine, FLinearColor(0.55f, 0.9f, 1.0f), TextX, BelowReadoutY, nullptr, 1.5f);
		BelowReadoutY += TextH + 18.0f;
	}
	else if (!JumpCardText.IsEmpty())
	{
		FString TitleLine;
		FString DetailLine;
		if (!JumpCardText.Split(TEXT("\n"), &TitleLine, &DetailLine))
		{
			TitleLine = JumpCardText;
		}
		// The failure cause (T2.6), when there is one, is its own line under the detail.
		FString CauseLine;
		{
			FString DetailOnly;
			if (DetailLine.Split(TEXT("\n"), &DetailOnly, &CauseLine))
			{
				DetailLine = DetailOnly;
			}
		}
		float TitleW = 0.0f;
		float TitleH = 0.0f;
		float DetailW = 0.0f;
		float DetailH = 0.0f;
		float CauseW = 0.0f;
		float CauseH = 0.0f;
		GetTextSize(TitleLine, TitleW, TitleH, nullptr, 1.5f);
		if (!DetailLine.IsEmpty())
		{
			GetTextSize(DetailLine, DetailW, DetailH, nullptr, 1.1f);
		}
		if (!CauseLine.IsEmpty())
		{
			GetTextSize(CauseLine, CauseW, CauseH, nullptr, 1.1f);
		}
		const float CardW = FMath::Max3(TitleW, DetailW, CauseW);
		const float CardH = TitleH + (DetailLine.IsEmpty() ? 0.0f : DetailH + 4.0f) + (CauseLine.IsEmpty() ? 0.0f : CauseH + 4.0f);
		DrawRect(FLinearColor(0.02f, 0.05f, 0.1f, 0.7f), ScreenW * 0.5f - CardW * 0.5f - 16.0f, BelowReadoutY - 6.0f, CardW + 32.0f, CardH + 12.0f);
		DrawText(TitleLine, GradeColor(JumpCardGrade), ScreenW * 0.5f - TitleW * 0.5f, BelowReadoutY, nullptr, 1.5f);
		float LineY = BelowReadoutY + TitleH + 4.0f;
		if (!DetailLine.IsEmpty())
		{
			DrawText(DetailLine, FLinearColor(0.8f, 0.85f, 0.9f), ScreenW * 0.5f - DetailW * 0.5f, LineY, nullptr, 1.1f);
			LineY += DetailH + 4.0f;
		}
		if (!CauseLine.IsEmpty())
		{
			DrawText(CauseLine, GradeColor(JumpCardGrade), ScreenW * 0.5f - CauseW * 0.5f, LineY, nullptr, 1.1f);
		}
		BelowReadoutY += CardH + 20.0f;
	}

	// The jump card just shown is also the first landing of its trick (review batch D, problem 7).
	const FString NewTrickText = GetNewTrickNoticeText();
	if (!NewTrickText.IsEmpty())
	{
		float NoticeW = 0.0f;
		float NoticeH = 0.0f;
		GetTextSize(NewTrickText, NoticeW, NoticeH, nullptr, 1.2f);
		DrawText(NewTrickText, FLinearColor(1.0f, 0.85f, 0.2f), ScreenW * 0.5f - NoticeW * 0.5f, BelowReadoutY - 6.0f, nullptr, 1.2f);
		BelowReadoutY += NoticeH + 10.0f;
	}

	if (ShouldShowNewBest())
	{
		const FString BestText = TEXT("NEW BEST");
		float BestW = 0.0f;
		float BestH = 0.0f;
		GetTextSize(BestText, BestW, BestH, nullptr, 1.2f);
		DrawText(BestText, FLinearColor(1.0f, 0.85f, 0.2f), ScreenW * 0.5f - BestW * 0.5f, BelowReadoutY - 6.0f, nullptr, 1.2f);
	}

	// The best-three session: its clock and counting jumps at the top, then its results card.
	DrawSession(ScreenW, ScreenH);

	// The freestyle heat (T3.6): its row at the top, the counting list at the right, then its results card.
	UpdateHeatNotice();
	DrawHeat(ScreenW, ScreenH);

	// A lesson's result card: stars, the result, the best, and Next / Retry / Lesson menu.
	LessonLayer.DrawResultCard(*this, ScreenW, ScreenH);

	if (JumpRejectionRemainingTime > 0.0f)
	{
		JumpRejectionRemainingTime = FMath::Max(0.0f, JumpRejectionRemainingTime - DeltaTime);

		if (!JumpRejectionText.IsEmpty())
		{
			float TextW = 0.0f;
			float TextH = 0.0f;
			GetTextSize(JumpRejectionText, TextW, TextH, nullptr, 1.2f);

			const float CenterX = ScreenW * 0.5f;
			const float TextX = CenterX - (TextW * 0.5f);
			const float TextY = (ScreenH - 50.0f) - 175.0f;

			DrawRect(FLinearColor(0.05f, 0.02f, 0.02f, 0.75f), TextX - 12.0f, TextY - 4.0f, TextW + 24.0f, TextH + 8.0f);
			DrawText(JumpRejectionText, FLinearColor(1.0f, 0.35f, 0.35f), TextX, TextY, nullptr, 1.2f);
		}
	}
}

void AKiteSurfHUD::DrawTelemetry(AKiteRiderPawn* RiderPawn)
{
	if (!RiderPawn)
	{
		return;
	}

	DrawRect(FLinearColor(0.02f, 0.05f, 0.1f, 0.65f), 20.0f, 20.0f, 330.0f, 226.0f);
	DrawText(TEXT("KITESURF TELEMETRY"), FLinearColor(1.0f, 0.85f, 0.2f), 32.0f, 28.0f, nullptr, 1.1f);

	FVector Vel = RiderPawn->GetBoardVelocity();
	FString SpeedStr = FString::Printf(TEXT("SPEED: %s"), *FormatKnots(Vel.Size2D()));
	DrawText(SpeedStr, FLinearColor::White, 32.0f, 50.0f, nullptr, 1.1f);

	float HeadingDeg = FRotator::NormalizeAxis(RiderPawn->GetActorRotation().Yaw);
	if (HeadingDeg < 0.0f)
	{
		HeadingDeg += 360.0f;
	}
	FString HeadingStr = FString::Printf(TEXT("HEADING: %.0f deg"), HeadingDeg);
	DrawText(HeadingStr, FLinearColor(0.85f, 0.95f, 1.0f), 32.0f, 72.0f, nullptr, 1.1f);

	UWindComponent* WindComp = RiderPawn->FindComponentByClass<UWindComponent>();
	// Sampled at the wind field's reference height (10 m), the wind forecasts quote: at chest height
	// the profile reads about 20% lower, and the kite aloft sees about 10% more.
	const FVector WindSampleLocation = RiderPawn->GetActorLocation() + FVector(0.0f, 0.0f, WindComp ? WindComp->ReferenceHeightCm : 0.0f);
	FVector WindVec = WindComp ? WindComp->GetWindAt(WindSampleLocation) : FVector(772.0f, 0.0f, 0.0f);
	// Gusts and lulls are called out: they are what the rider has to sheet and steer for.
	const float GustFactor = WindComp ? WindComp->GetGustFactorAt(RiderPawn->GetActorLocation()) : 1.0f;
	const TCHAR* GustLabel = GustFactor > 1.12f ? TEXT("  GUST") : (GustFactor < 0.88f ? TEXT("  LULL") : TEXT(""));
	const FLinearColor WindColor = GustFactor > 1.12f ? FLinearColor(1.0f, 0.6f, 0.2f) : (GustFactor < 0.88f ? FLinearColor(0.6f, 0.7f, 0.8f) : FLinearColor(0.3f, 0.8f, 1.0f));
	const UKiteComponent* RiggedKite = RiderPawn->GetKite();
	const FString KiteSizeStr = RiggedKite ? FString::Printf(TEXT("  |  KITE %.0f m"), RiggedKite->AreaM2) : FString();
	FString WindStr = FString::Printf(TEXT("WIND:  %s%s%s"), *FormatKnots(WindVec.Size()), *KiteSizeStr, GustLabel);
	DrawText(WindStr, WindColor, 32.0f, 94.0f, nullptr, 1.1f);

	if (const UBoardMovementComponent* BoardMove = RiderPawn->GetBoardMovement())
	{
		const EBoardState BoardState = BoardMove->GetBoardState();
		FString StateStr = BoardMove->IsFloating() ? TEXT("STATE: Floating - get the kite pulling") : TEXT("STATE: Getting up");
		if (BoardState == EBoardState::Planing)
		{
			StateStr = BoardMove->GetLoadAmount() > 0.05f
				? FString::Printf(TEXT("STATE: Planing  -  LOADING %.0f%%"), BoardMove->GetLoadAmount() * 100.0f)
				: FString(TEXT("STATE: Planing"));
		}
		else if (BoardState == EBoardState::Airborne)
		{
			StateStr = FString::Printf(TEXT("STATE: Airborne (%.1fm)"), BoardMove->GetCurrentJumpHeight() / 100.0f);
		}
		else if (BoardState == EBoardState::Landing)
		{
			StateStr = BoardMove->IsCrashing() ? TEXT("STATE: Crash!") : TEXT("STATE: Clean Landing");
		}
		DrawText(StateStr, FLinearColor(1.0f, 0.85f, 0.2f), 32.0f, 120.0f, nullptr, 1.1f);

		FString JumpStr = FString::Printf(TEXT("JUMP: Best %.1f m high, %.0f m far | Last %.1f m, %.0f m"),
			KiteUnits::CmToM(BoardMove->GetBestJumpHeight()),
			KiteUnits::CmToM(BoardMove->GetBestJumpDistance()),
			KiteUnits::CmToM(BoardMove->GetLastJumpApexHeight()),
			KiteUnits::CmToM(BoardMove->GetLastJumpDistance()));
		DrawText(JumpStr, FLinearColor(0.85f, 0.95f, 1.0f), 32.0f, 144.0f, nullptr, 1.1f);
	}

	const FString SheetStr = FString::Printf(TEXT("BAR: %.0f%% sheeted in"), RiderPawn->GetCurrentSheetInput() * 100.0f);
	DrawText(SheetStr, FLinearColor(0.85f, 0.95f, 1.0f), 32.0f, 168.0f, nullptr, 1.1f);

	if (const UKiteComponent* KiteComp = RiderPawn->GetKite())
	{
		if (!KiteComp->IsCrashed() && !KiteComp->AreLinesTaut())
		{
			DrawText(TEXT("LINES SLACK - kite falling"), FLinearColor(1.0f, 0.35f, 0.35f), 32.0f, 192.0f, nullptr, 1.2f);
		}
		else if (!KiteComp->IsCrashed() && KiteComp->GetAngleOfAttackDeg() > KiteComp->StallAngleDeg)
		{
			DrawText(TEXT("KITE STALLED - sheet out"), FLinearColor(1.0f, 0.6f, 0.2f), 32.0f, 192.0f, nullptr, 1.2f);
		}

		if (KiteComp->IsCrashed())
		{
			DrawText(FString::Printf(TEXT("KITE DOWN - relaunch in %.0f s (or steer)"), FMath::CeilToFloat(KiteComp->GetRelaunchSecondsRemaining())),
				FLinearColor(1.0f, 0.35f, 0.35f), 32.0f, 192.0f, nullptr, 1.2f);
		}

		const int32 Loops = FMath::FloorToInt(FMath::Abs(KiteComp->GetTurnDeg()) / 360.0f);
		if (Loops > 0 && !KiteComp->IsCrashed() && KiteComp->AreLinesTaut())
		{
			DrawText(FString::Printf(TEXT("KITE LOOP x%d"), Loops), FLinearColor(1.0f, 0.5f, 0.1f), 32.0f, 192.0f, nullptr, 1.2f);
		}
	}
}

FVector2D AKiteSurfHUD::GetWindOnScreen(const FVector& Wind, float CameraYawDeg)
{
	const FVector Wind2D = Wind.GetSafeNormal2D();
	if (Wind2D.IsNearlyZero())
	{
		return FVector2D::ZeroVector;
	}
	const FVector Forward = FRotator(0.0f, CameraYawDeg, 0.0f).Vector();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
	// Screen y grows downwards, and "up" is the way the camera looks.
	return FVector2D(FVector::DotProduct(Wind2D, Right), -FVector::DotProduct(Wind2D, Forward));
}

FString AKiteSurfHUD::DescribeWindSource(const FVector2D& WindOnScreen)
{
	if (WindOnScreen.IsNearlyZero())
	{
		return TEXT("calm");
	}
	// The wind comes from the opposite side to the one it blows towards.
	const FVector2D From = -WindOnScreen;
	const float AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(From.X, -From.Y)); // 0 ahead, 90 right
	if (FMath::Abs(AngleDeg) <= 30.0f)
	{
		return TEXT("from ahead");
	}
	if (FMath::Abs(AngleDeg) >= 150.0f)
	{
		return TEXT("from behind");
	}
	const TCHAR* Side = AngleDeg > 0.0f ? TEXT("right") : TEXT("left");
	if (FMath::Abs(AngleDeg) < 60.0f)
	{
		return FString::Printf(TEXT("from ahead %s"), Side);
	}
	if (FMath::Abs(AngleDeg) > 120.0f)
	{
		return FString::Printf(TEXT("from behind %s"), Side);
	}
	return FString::Printf(TEXT("from the %s"), Side);
}

void AKiteSurfHUD::DrawWindFlag(AKiteRiderPawn* RiderPawn, float ScreenX, float ScreenY, float Size)
{
	const UWindComponent* WindComp = RiderPawn ? RiderPawn->GetWind() : nullptr;
	const APlayerController* PC = GetOwningPlayerController();
	if (!WindComp || !PC || !PC->PlayerCameraManager)
	{
		return;
	}

	// The same wind the telemetry reads: at the reference height, not slowed by the water.
	const FVector Wind = WindComp->GetWindAt(RiderPawn->GetActorLocation() + FVector(0.0f, 0.0f, WindComp->ReferenceHeightCm));
	const FVector2D OnScreen = GetWindOnScreen(Wind, PC->PlayerCameraManager->GetCameraRotation().Yaw);
	const float Knots = KiteUnits::CmSToKnots(Wind.Size2D());

	// A flag on a pole seen from above, with the top of the dial the way you are looking. The
	// flag streams the way the wind blows, longer the harder it blows.
	DrawRect(FLinearColor(0.02f, 0.05f, 0.1f, 0.65f), ScreenX, ScreenY, Size, Size + 44.0f);
	DrawText(TEXT("WIND"), FLinearColor(1.0f, 0.85f, 0.2f), ScreenX + 10.0f, ScreenY + 6.0f, nullptr, 1.0f);
	DrawText(FormatKnots(Wind.Size2D()), FLinearColor(0.3f, 0.8f, 1.0f), ScreenX + Size - 62.0f, ScreenY + 6.0f, nullptr, 1.0f);

	const FVector2D Centre(ScreenX + Size * 0.5f, ScreenY + 28.0f + (Size - 28.0f) * 0.5f);
	const float Radius = (Size - 44.0f) * 0.5f;
	const int32 Segments = 32;
	for (int32 Index = 0; Index < Segments; ++Index)
	{
		const float A0 = 2.0f * UE_PI * Index / Segments;
		const float A1 = 2.0f * UE_PI * (Index + 1) / Segments;
		DrawLine(Centre.X + Radius * FMath::Cos(A0), Centre.Y + Radius * FMath::Sin(A0), Centre.X + Radius * FMath::Cos(A1), Centre.Y + Radius * FMath::Sin(A1), FLinearColor(0.4f, 0.7f, 1.0f, 0.6f), 1.5f);
	}
	// The way you are looking
	DrawLine(Centre.X, Centre.Y - Radius - 5.0f, Centre.X, Centre.Y - Radius + 7.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.8f), 2.0f);

	if (!OnScreen.IsNearlyZero())
	{
		const FVector2D Across(-OnScreen.Y, OnScreen.X);
		const float FlagLength = Radius * FMath::Clamp(0.45f + 0.55f * Knots / 30.0f, 0.45f, 1.0f);
		const float Seconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		const float Flutter = FMath::Sin(Seconds * (4.0f + Knots * 0.25f)) * 0.12f;
		const FVector2D Tip = Centre + (OnScreen + Across * Flutter).GetSafeNormal() * FlagLength;
		const FLinearColor FlagColor(1.0f, 0.45f, 0.15f);
		// A filled pennant: lines fanned from its root across the pole to its tip.
		const float RootHalfWidth = 9.0f;
		for (float T = -1.0f; T <= 1.0f; T += 0.2f)
		{
			const FVector2D Root = Centre + Across * RootHalfWidth * T;
			DrawLine(Root.X, Root.Y, Tip.X, Tip.Y, FlagColor, 2.5f);
		}
		DrawRect(FLinearColor::White, Centre.X - 3.0f, Centre.Y - 3.0f, 6.0f, 6.0f); // the pole
	}

	const FString Source = DescribeWindSource(OnScreen);
	float TextW = 0.0f;
	float TextH = 0.0f;
	GetTextSize(Source, TextW, TextH, nullptr, 0.9f);
	DrawText(Source, FLinearColor::White, ScreenX + (Size - TextW) * 0.5f, ScreenY + Size + 20.0f, nullptr, 0.9f);
}

void AKiteSurfHUD::DrawWindWindowArc(AKiteRiderPawn* RiderPawn, float CenterX, float CenterY, float Radius)
{
	const UKiteComponent* Kite = RiderPawn ? RiderPawn->GetKite() : nullptr;
	if (!Kite)
	{
		return;
	}

	DrawRect(FLinearColor(0.02f, 0.05f, 0.1f, 0.65f), CenterX - 160.0f, CenterY - 145.0f, 320.0f, 160.0f);

	const FString Title = FString::Printf(TEXT("WIND WINDOW   AZ %+.0f   EL %.0f"), Kite->GetAzimuthDeg(), Kite->GetElevationDeg());
	DrawText(Title, FLinearColor(1.0f, 0.85f, 0.2f), CenterX - 140.0f, CenterY - 135.0f, nullptr, 1.0f);

	// The window seen from the rider looking downwind: the arc is its edge, from the left horizon
	// over the zenith to the right horizon, and the water is the base line.
	const int32 NumSegments = 30;
	for (int32 i = 0; i < NumSegments; ++i)
	{
		const float Rad1 = FMath::DegreesToRadians(180.0f + (180.0f * i) / NumSegments);
		const float Rad2 = FMath::DegreesToRadians(180.0f + (180.0f * (i + 1)) / NumSegments);
		DrawLine(CenterX + Radius * FMath::Cos(Rad1), CenterY + Radius * FMath::Sin(Rad1),
			CenterX + Radius * FMath::Cos(Rad2), CenterY + Radius * FMath::Sin(Rad2), FLinearColor(0.4f, 0.7f, 1.0f, 0.8f), 2.0f);
	}
	DrawLine(CenterX - Radius, CenterY, CenterX + Radius, CenterY, FLinearColor(0.4f, 0.7f, 1.0f, 0.5f), 1.5f);

	// Clock ticks at 9, 10:30, 12, 1:30 and 3
	const float ClockTicksDeg[] = { -90.0f, -45.0f, 0.0f, 45.0f, 90.0f };
	for (float TickDeg : ClockTicksDeg)
	{
		const float Rad = FMath::DegreesToRadians(270.0f + TickDeg);
		DrawLine(CenterX + (Radius - 8.0f) * FMath::Cos(Rad), CenterY + (Radius - 8.0f) * FMath::Sin(Rad),
			CenterX + (Radius + 8.0f) * FMath::Cos(Rad), CenterY + (Radius + 8.0f) * FMath::Sin(Rad), FLinearColor(0.8f, 0.9f, 1.0f), 1.5f);
	}

	// The kite: sideways and upward parts of its direction, so it sits on the arc at the window
	// edge and moves towards the middle as it goes deeper into the window.
	const float ClockRad = FMath::DegreesToRadians(Kite->GetClockDeg());
	const float RingRadius = Radius * FMath::Cos(FMath::DegreesToRadians(Kite->GetWindowDepthDeg()));
	const float MarkerX = CenterX + RingRadius * FMath::Sin(ClockRad);
	const float MarkerY = CenterY - RingRadius * FMath::Cos(ClockRad);
	DrawLine(CenterX, CenterY, MarkerX, MarkerY, FLinearColor(1.0f, 0.5f, 0.1f, 0.6f), 1.5f);
	DrawRect(FLinearColor(1.0f, 0.3f, 0.0f, 1.0f), MarkerX - 6.0f, MarkerY - 6.0f, 12.0f, 12.0f);
	DrawRect(FLinearColor(1.0f, 0.9f, 0.2f, 1.0f), MarkerX - 3.0f, MarkerY - 3.0f, 6.0f, 6.0f);

	// A lesson's cue on the arc: target zones, the ghost kite, the timing ring around the marker.
	LessonLayer.DrawArcCue(*this, FVector2D(CenterX, CenterY), Radius);
}

FString AKiteSurfHUD::FormatFPS(float RealFrameSeconds)
{
	return FString::Printf(TEXT("FPS: %.0f"), RealFrameSeconds > 0.0f ? 1.0f / RealFrameSeconds : 0.0f);
}

float AKiteSurfHUD::GetRealFrameSeconds(const UWorld* World)
{
	return World ? World->DeltaRealTimeSeconds : 0.0f;
}

bool AKiteSurfHUD::IsNewBestJump(float ApexCm, float BestLandedBeforeCm, bool bLandedClean)
{
	return bLandedClean && BestLandedBeforeCm > 0.0f && ApexCm > BestLandedBeforeCm;
}

bool AKiteSurfHUD::ShouldShowNewBest() const
{
	const bool bCrashCard = !JumpCardText.IsEmpty() && JumpCardGrade == ELandingGrade::Crash;
	return !JumpReadoutText.IsEmpty() && bJumpReadoutNewBest && !bJumpReadoutLive && !bCrashCard;
}

void AKiteSurfHUD::DrawFPS(float ScreenX, float ScreenY)
{
	const FString FPSText = FormatFPS(GetRealFrameSeconds(GetWorld()));
	DrawRect(FLinearColor(0.02f, 0.05f, 0.1f, 0.5f), ScreenX - 10.0f, ScreenY - 5.0f, 110.0f, 30.0f);
	DrawText(FPSText, FLinearColor::Green, ScreenX, ScreenY, nullptr, 1.1f);
}

void AKiteSurfHUD::DrawPowerGauge(AKiteRiderPawn* RiderPawn, float ScreenX, float ScreenY, float Width, float Height)
{
	if (!RiderPawn)
	{
		return;
	}

	UKiteComponent* Kite = RiderPawn->GetKite();
	const float LineTensionN = Kite ? Kite->GetLineTensionN() : 0.0f;
	// Reference rider weight tension: 75 kg * 9.81 m/s^2 = ~735 N (full rider lift)
	const float MaxTensionReference = 1200.0f;
	const float TensionFraction = FMath::Clamp(LineTensionN / MaxTensionReference, 0.0f, 1.0f);

	// Draw background box
	DrawRect(FLinearColor(0.02f, 0.05f, 0.1f, 0.75f), ScreenX - 60.0f, ScreenY - 30.0f, Width + 70.0f, Height + 45.0f);
	DrawText(TEXT("POWER"), FLinearColor(1.0f, 0.85f, 0.2f), ScreenX - 50.0f, ScreenY - 24.0f, nullptr, 1.0f);

	// Gauge track background
	DrawRect(FLinearColor(0.1f, 0.12f, 0.15f, 0.9f), ScreenX, ScreenY, Width, Height);

	// Fill from bottom up
	const float FilledHeight = Height * TensionFraction;
	const float FillTopY = ScreenY + (Height - FilledHeight);

	// Color transitions: Green (low) -> Yellow (moderate) -> Orange/Red (high power zone)
	FLinearColor BarColor;
	if (TensionFraction < 0.4f)
	{
		BarColor = FMath::Lerp(FLinearColor(0.2f, 0.8f, 0.3f), FLinearColor(0.9f, 0.9f, 0.2f), TensionFraction / 0.4f);
	}
	else if (TensionFraction < 0.75f)
	{
		BarColor = FMath::Lerp(FLinearColor(0.9f, 0.9f, 0.2f), FLinearColor(1.0f, 0.5f, 0.1f), (TensionFraction - 0.4f) / 0.35f);
	}
	else
	{
		BarColor = FMath::Lerp(FLinearColor(1.0f, 0.5f, 0.1f), FLinearColor(1.0f, 0.2f, 0.2f), (TensionFraction - 0.75f) / 0.25f);
	}

	DrawRect(BarColor, ScreenX + 2.0f, FillTopY, Width - 4.0f, FilledHeight);

	// Tick marks for reference
	// 100% rider weight lift (~735 N / 1200 N = 0.6125)
	const float RiderWeightY = ScreenY + Height * (1.0f - (735.0f / MaxTensionReference));
	DrawLine(ScreenX - 6.0f, RiderWeightY, ScreenX + Width + 6.0f, RiderWeightY, FLinearColor::White, 2.0f);
	DrawText(TEXT("1G"), FLinearColor::White, ScreenX - 25.0f, RiderWeightY - 7.0f, nullptr, 0.8f);

	// Tension numeric display
	FString TensionStr = FString::Printf(TEXT("%.0f N"), LineTensionN);
	DrawText(TensionStr, FLinearColor::White, ScreenX - 45.0f, ScreenY + Height - 16.0f, nullptr, 0.9f);
}

void AKiteSurfHUD::GetBarEnds(float Steer, float Sheet, const FVector2D& ThrowTop, float ThrowLength, float HalfWidth, float MaxTiltDeg, FVector2D& OutLeftEnd, FVector2D& OutRightEnd)
{
	const FVector2D BarCentre(ThrowTop.X, ThrowTop.Y + FMath::Clamp(Sheet, 0.0f, 1.0f) * ThrowLength);
	const float TiltRad = FMath::DegreesToRadians(FMath::Clamp(Steer, -1.0f, 1.0f) * MaxTiltDeg);
	const FVector2D HalfBar(HalfWidth * FMath::Cos(TiltRad), HalfWidth * FMath::Sin(TiltRad));
	OutLeftEnd = BarCentre - HalfBar;
	OutRightEnd = BarCentre + HalfBar;
}

float AKiteSurfHUD::ComputeGripMeterFraction(float TensionN, float BodyWeightN, float GripLimitBW)
{
	return FMath::Max(TensionN, 0.0f) / FMath::Max(BodyWeightN, 1.0f) / FMath::Max(GripLimitBW, 0.01f);
}

void AKiteSurfHUD::DrawControlBar(AKiteRiderPawn* RiderPawn, float ScreenX, float ScreenY, float Width, float Height)
{
	UKiteComponent* Kite = RiderPawn ? RiderPawn->GetKite() : nullptr;
	if (!Kite)
	{
		return;
	}

	const float Steer = RiderPawn->GetCurrentSteerInput();
	const float Sheet = RiderPawn->GetCurrentSheetInput();
	const bool bLooping = Kite->IsLooping();
	const bool bFlying = !Kite->IsCrashed() && Kite->AreLinesTaut();

	DrawRect(FLinearColor(0.02f, 0.05f, 0.1f, 0.75f), ScreenX, ScreenY, Width, Height);
	DrawText(RiderPawn->IsMotionBarActive() ? TEXT("BAR  (motion)") : TEXT("BAR"), FLinearColor(1.0f, 0.85f, 0.2f), ScreenX + 10.0f, ScreenY + 6.0f, nullptr, 1.0f);
	if (bLooping)
	{
		DrawText(TEXT("LOOP"), FLinearColor(1.0f, 0.5f, 0.1f), ScreenX + Width - 50.0f, ScreenY + 6.0f, nullptr, 1.0f);
	}

	const FBarState& BarState = RiderPawn->GetBarState();
	const bool bUnhooked = !BarState.bHooked;
	const bool bBarLost = RiderPawn->IsBarLost();

	// The throw: the centre lines the bar slides on, depowered at the top, pulled in at the bottom.
	const float CentreX = ScreenX + Width * 0.5f;
	// The throw starts far enough down that a fully tilted, sheeted-out bar stays below the line tops.
	const FVector2D ThrowTop(CentreX, ScreenY + 78.0f);
	const float ThrowLength = 68.0f;
	const float HalfBar = 68.0f;
	const float MaxTiltDeg = 22.0f;
	const float LinesTopY = ScreenY + 28.0f;
	const float ThrowBottomY = ThrowTop.Y + ThrowLength;

	// Lines glow with the load in them and go dull when slack.
	const float Load = FMath::Clamp(Kite->GetLineTensionN() / 1200.0f, 0.0f, 1.0f);
	const FLinearColor LineColor = bFlying
		? FMath::Lerp(FLinearColor(0.55f, 0.7f, 0.8f, 0.9f), FLinearColor(1.0f, 0.45f, 0.15f, 1.0f), Load)
		: FLinearColor(0.45f, 0.3f, 0.3f, 0.7f);

	FVector2D LeftEnd;
	FVector2D RightEnd;
	GetBarEnds(Steer, Sheet, ThrowTop, ThrowLength, HalfBar, MaxTiltDeg, LeftEnd, RightEnd);

	// Front lines down the middle to the chicken loop, steering lines from the bar ends up to the kite.
	DrawLine(CentreX, LinesTopY, CentreX, ThrowBottomY + 18.0f, LineColor, 2.0f);
	DrawLine(LeftEnd.X, LeftEnd.Y, CentreX - 26.0f, LinesTopY, LineColor, 1.5f);
	DrawLine(RightEnd.X, RightEnd.Y, CentreX + 26.0f, LinesTopY, LineColor, 1.5f);
	DrawRect(FLinearColor(0.75f, 0.8f, 0.85f, 0.9f), CentreX - 5.0f, ThrowBottomY + 18.0f, 10.0f, 10.0f);

	// The ends of the throw
	DrawLine(CentreX - 8.0f, ThrowTop.Y, CentreX + 8.0f, ThrowTop.Y, FLinearColor(1.0f, 1.0f, 1.0f, 0.5f), 1.0f);
	DrawLine(CentreX - 8.0f, ThrowBottomY, CentreX + 8.0f, ThrowBottomY, FLinearColor(1.0f, 1.0f, 1.0f, 0.5f), 1.0f);
	DrawText(TEXT("out"), FLinearColor(0.7f, 0.8f, 0.9f, 0.8f), ScreenX + 10.0f, ThrowTop.Y - 7.0f, nullptr, 0.8f);
	DrawText(TEXT("in"), FLinearColor(0.7f, 0.8f, 0.9f, 0.8f), ScreenX + 10.0f, ThrowBottomY - 7.0f, nullptr, 0.8f);

	// The bar itself: red on the left hand as on a real bar, lit up while looping.
	const FVector2D BarCentre = (LeftEnd + RightEnd) * 0.5f;
	const FLinearColor RightColor = bLooping ? FLinearColor(1.0f, 0.6f, 0.15f) : FLinearColor(0.85f, 0.88f, 0.92f);
	DrawLine(LeftEnd.X, LeftEnd.Y, BarCentre.X, BarCentre.Y, FLinearColor(0.95f, 0.2f, 0.2f), 7.0f);
	DrawLine(BarCentre.X, BarCentre.Y, RightEnd.X, RightEnd.Y, RightColor, 7.0f);

	// Steering scale: the rider's bar (filled) and what actually reaches the kite (marker). They
	// differ while the assist is flying the kite, and match while looping.
	const float TrackY = ScreenY + Height - 58.0f;
	const float TrackHalf = Width * 0.5f - 20.0f;
	DrawRect(FLinearColor(0.1f, 0.12f, 0.15f, 0.9f), CentreX - TrackHalf, TrackY, TrackHalf * 2.0f, 8.0f);
	const float FillW = TrackHalf * FMath::Abs(FMath::Clamp(Steer, -1.0f, 1.0f));
	DrawRect(FLinearColor(0.2f, 0.85f, 1.0f), Steer < 0.0f ? CentreX - FillW : CentreX, TrackY, FillW, 8.0f);
	DrawLine(CentreX, TrackY - 3.0f, CentreX, TrackY + 11.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.7f), 1.0f);
	const float KiteMarkX = CentreX + TrackHalf * FMath::Clamp(Kite->GetAppliedSteer(), -1.0f, 1.0f);
	DrawRect(FLinearColor(1.0f, 0.85f, 0.2f), KiteMarkX - 2.0f, TrackY - 4.0f, 4.0f, 16.0f);

	const TCHAR* SteerSide = Steer < -0.05f ? TEXT("L") : (Steer > 0.05f ? TEXT("R") : TEXT("-"));
	DrawText(FString::Printf(TEXT("STEER %s %.0f%%"), SteerSide, FMath::Abs(Steer) * 100.0f), FLinearColor(0.2f, 0.85f, 1.0f), ScreenX + 10.0f, TrackY + 14.0f, nullptr, 0.9f);
	DrawText(TEXT("kite"), FLinearColor(1.0f, 0.85f, 0.2f), ScreenX + Width - 42.0f, TrackY + 14.0f, nullptr, 0.9f);
	// The hook (T3.1), bottom right: a closed hook while hooked in, an open one unhooked, crossed out
	// once the bar is lost.
	{
		const float HookX = ScreenX + Width - 96.0f;
		const float HookTop = TrackY + 30.0f;
		const FLinearColor HookColor = bBarLost ? FLinearColor(1.0f, 0.25f, 0.2f) : (bUnhooked ? FLinearColor(1.0f, 0.6f, 0.15f) : FLinearColor(0.45f, 0.9f, 0.5f));
		// The shank, the curve of the hook as three segments, and the gate: shut while hooked in.
		DrawLine(HookX, HookTop, HookX, HookTop + 10.0f, HookColor, 2.0f);
		DrawLine(HookX, HookTop + 10.0f, HookX + 3.0f, HookTop + 14.0f, HookColor, 2.0f);
		DrawLine(HookX + 3.0f, HookTop + 14.0f, HookX + 8.0f, HookTop + 14.0f, HookColor, 2.0f);
		DrawLine(HookX + 8.0f, HookTop + 14.0f, HookX + 10.0f, HookTop + 9.0f, HookColor, 2.0f);
		if (!bUnhooked)
		{
			DrawLine(HookX + 10.0f, HookTop + 9.0f, HookX, HookTop + 4.0f, HookColor, 1.5f);
		}
		if (bBarLost)
		{
			DrawLine(HookX - 3.0f, HookTop - 1.0f, HookX + 13.0f, HookTop + 16.0f, HookColor, 2.0f);
		}
		DrawText(bBarLost ? TEXT("BAR LOST") : (bUnhooked ? TEXT("UNHOOKED") : TEXT("HOOKED")), HookColor, HookX + 16.0f, HookTop + 1.0f, nullptr, 0.8f);
	}

	if (bUnhooked)
	{
		// Unhooked the bar moves the arms; the kite's sheet is held at the stopper.
		DrawText(FString::Printf(TEXT("ARMS OUT %.0f%%"), RiderPawn->GetArmExtension() * 100.0f), FLinearColor::White, ScreenX + 10.0f, TrackY + 32.0f, nullptr, 0.9f);

		// The grip meter, up the right of the panel: the pull against the grip limit (the tick), green,
		// orange near it, red past it, and the hands' slip filling the frame while over it.
		const UBoardMovementComponent* Board = RiderPawn->GetBoardMovement();
		const float BodyWeightN = (Board ? Board->MassKg : 85.0f) * KiteUnits::GravityMS2;
		const float Grip = bBarLost ? 0.0f : ComputeGripMeterFraction(Kite->GetLineTensionN(), BodyWeightN, RiderPawn->BarTunables.GripLimitBW);
		const float MeterX = ScreenX + Width - 22.0f;
		const float MeterTop = LinesTopY;
		const float MeterHeight = ThrowBottomY + 28.0f - LinesTopY;
		const float FullScale = 1.5f; // the meter's top is 1.5 times the grip limit
		const float Fill = FMath::Clamp(Grip / FullScale, 0.0f, 1.0f) * MeterHeight;
		const FLinearColor GripColor = Grip > 1.0f ? FLinearColor(1.0f, 0.2f, 0.15f) : (Grip > 0.8f ? FLinearColor(1.0f, 0.6f, 0.15f) : FLinearColor(0.35f, 0.9f, 0.45f));
		DrawRect(FLinearColor(0.1f, 0.12f, 0.15f, 0.9f), MeterX, MeterTop, 10.0f, MeterHeight);
		DrawRect(GripColor, MeterX, MeterTop + MeterHeight - Fill, 10.0f, Fill);
		const float LimitY = MeterTop + MeterHeight * (1.0f - 1.0f / FullScale);
		DrawLine(MeterX - 3.0f, LimitY, MeterX + 13.0f, LimitY, FLinearColor::White, 1.5f);
		const float Slip = FMath::Clamp(BarState.OverGripSeconds / FMath::Max(RiderPawn->BarTunables.GripLimitSeconds, 0.01f), 0.0f, 1.0f);
		if (Slip > 0.0f)
		{
			DrawRect(FLinearColor(1.0f, 0.2f, 0.15f, 0.35f + 0.5f * Slip), MeterX - 2.0f, MeterTop - 2.0f, 14.0f, 4.0f);
		}
		DrawText(TEXT("GRIP"), GripColor, MeterX - 8.0f, MeterTop + MeterHeight + 2.0f, nullptr, 0.7f);
	}
	else
	{
		DrawText(FString::Printf(TEXT("PULLED IN %.0f%%"), Sheet * 100.0f), FLinearColor::White, ScreenX + 10.0f, TrackY + 32.0f, nullptr, 0.9f);
	}
}

void AKiteSurfHUD::UpdateTutorialHint()
{
	TutorialHintLines.Reset();
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	if (const USchoolOnboardingSubsystem* Onboarding = GameInstance ? GameInstance->GetSubsystem<USchoolOnboardingSubsystem>() : nullptr)
	{
		TutorialHintLines = Onboarding->GetHintLines(FindLessonDirector());
	}
}

float AKiteSurfHUD::DrawTutorialHint(float ScreenW, float ScreenH, float Top)
{
	if (TutorialHintLines.Num() == 0)
	{
		return Top;
	}
	// Sized like the lesson panel (LessonHUD's UiScale), centred and clear of the telemetry and the wind flag.
	const float Ui = FMath::Clamp(ScreenH / 1080.0f, 0.85f, 1.6f);
	const float Pad = 10.0f * Ui;
	const float Spacing = 4.0f * Ui;
	const float LeftLimit = 365.0f;
	const float RightLimit = FMath::Max(ScreenW - 185.0f, LeftLimit + 200.0f);
	const float MaxInnerW = FMath::Min(760.0f * Ui, RightLimit - LeftLimit) - 2.0f * Pad;
	TArray<float> Scales;
	TArray<FVector2D> Sizes;
	float InnerW = 0.0f;
	float InnerH = 0.0f;
	for (int32 I = 0; I < TutorialHintLines.Num(); ++I)
	{
		// The first line is the title: larger.
		float Scale = (I == 0 ? 1.25f : 1.05f) * Ui;
		float W = 0.0f;
		float H = 0.0f;
		GetTextSize(TutorialHintLines[I], W, H, nullptr, Scale);
		if (W > MaxInnerW && W > 0.0f)
		{
			Scale *= MaxInnerW / W;
			GetTextSize(TutorialHintLines[I], W, H, nullptr, Scale);
		}
		Scales.Add(Scale);
		Sizes.Add(FVector2D(W, H));
		InnerW = FMath::Max(InnerW, W);
		InnerH += H + Spacing;
	}
	InnerH -= Spacing;
	const float BoxW = InnerW + 2.0f * Pad;
	const float BoxH = InnerH + 2.0f * Pad;
	const float BoxX = FMath::Clamp(ScreenW * 0.5f - BoxW * 0.5f, LeftLimit, FMath::Max(RightLimit - BoxW, LeftLimit));
	const FLinearColor Green(0.45f, 1.0f, 0.6f);
	DrawRect(FLinearColor(0.02f, 0.08f, 0.06f, 0.85f), BoxX, Top, BoxW, BoxH);
	DrawRect(FLinearColor(Green.R, Green.G, Green.B, 0.9f), BoxX, Top, 3.0f * Ui, BoxH);
	float Y = Top + Pad;
	for (int32 I = 0; I < TutorialHintLines.Num(); ++I)
	{
		DrawText(TutorialHintLines[I], I == 0 ? Green : FLinearColor(0.85f, 0.92f, 0.95f), BoxX + Pad, Y, nullptr, Scales[I]);
		Y += Sizes[I].Y + Spacing;
	}
	return Top + BoxH;
}

FString AKiteSurfHUD::FormatSessionClock(float SecondsLeft)
{
	const int32 Whole = FMath::Max(FMath::CeilToInt(SecondsLeft - KINDA_SMALL_NUMBER), 0);
	return FString::Printf(TEXT("%d:%02d"), Whole / 60, Whole % 60);
}

FString AKiteSurfHUD::FormatSessionSlot(const FJumpRecord& Counting)
{
	FString Slot = FString::Printf(TEXT("%.1f"), FBestThreeSession::Paid(Counting));
	if (Counting.RepeatFactor < 0.999f)
	{
		Slot += FString::Printf(TEXT(" x%.2f"), Counting.RepeatFactor);
	}
	return Slot;
}

TArray<FString> AKiteSurfHUD::FormatSessionPanel(const FBestThreeSession& Session)
{
	TArray<FString> Lines;
	FString Header = FString::Printf(TEXT("SESSION %s"), *FormatSessionClock(Session.GetTimeLeft()));
	if (Session.GetPhase() == EBestThreePhase::Overtime)
	{
		Header += TEXT("  OVERTIME");
	}
	Lines.Add(Header);
	const TArray<FJumpRecord> Counting = Session.GetCounting();
	for (int32 Slot = 0; Slot < FMath::Max(Session.Settings.CountingJumps, 0); ++Slot)
	{
		Lines.Add(FString::Printf(TEXT("%d  %s"), Slot + 1, Counting.IsValidIndex(Slot) ? *FormatSessionSlot(Counting[Slot]) : TEXT("--")));
	}
	return Lines;
}

TArray<FString> AKiteSurfHUD::FormatSessionResults(const FBestThreeSession& Session, float PreviousBest, bool bHadPreviousBest, bool bNewBest)
{
	TArray<FString> Lines;
	const int32 Seconds = FMath::RoundToInt(Session.GetDuration());
	Lines.Add(FString::Printf(TEXT("SESSION OVER  (%d s)"), Seconds));
	Lines.Add(FString::Printf(TEXT("TOTAL  %.1f"), Session.GetTotal()));
	if (bNewBest)
	{
		Lines.Add(TEXT("NEW BEST"));
	}
	const TArray<FJumpRecord> Counting = Session.GetCounting();
	for (int32 Rank = 0; Rank < Counting.Num(); ++Rank)
	{
		const FJumpRecord& Jump = Counting[Rank];
		Lines.Add(FString::Printf(TEXT("%d  %s  %.1f m  %.1f pts"), Rank + 1, Jump.TrickName.IsEmpty() ? TEXT("Jump") : *Jump.TrickName,
			KiteUnits::CmToM(Jump.ApexHeightCm), FBestThreeSession::Paid(Jump)));
	}
	if (Counting.Num() == 0)
	{
		Lines.Add(TEXT("No jumps counted"));
	}
	if (!bHadPreviousBest)
	{
		Lines.Add(FString::Printf(TEXT("First %d s session"), Seconds));
	}
	else
	{
		Lines.Add(FString::Printf(TEXT("%s  %.1f"), bNewBest ? TEXT("Previous best") : TEXT("Local best"), PreviousBest));
	}
	return Lines;
}

TArray<FString> AKiteSurfHUD::GetSessionLines() const
{
	const UWorld* World = GetWorld();
	const UTrickSessionSubsystem* Sessions = World ? World->GetSubsystem<UTrickSessionSubsystem>() : nullptr;
	if (!Sessions)
	{
		return TArray<FString>();
	}
	if (Sessions->IsSessionActive())
	{
		return FormatSessionPanel(Sessions->GetSession());
	}
	if (Sessions->IsShowingResults())
	{
		return FormatSessionResults(Sessions->GetSession(), Sessions->GetPreviousBest(), Sessions->HadPreviousBest(), Sessions->IsNewBest());
	}
	return TArray<FString>();
}

void AKiteSurfHUD::DrawSession(float ScreenW, float ScreenH)
{
	const UWorld* World = GetWorld();
	const UTrickSessionSubsystem* Sessions = World ? World->GetSubsystem<UTrickSessionSubsystem>() : nullptr;
	const TArray<FString> Lines = GetSessionLines();
	if (!Sessions || Lines.Num() == 0)
	{
		return;
	}
	// Sized and placed from the screen height, inside a safe-area margin, so it holds its place at
	// any resolution.
	const float UiScale = FMath::Clamp(ScreenH / 1080.0f, 0.85f, 1.6f);
	const float Margin = FMath::Max(16.0f, ScreenH * 0.02f);
	const FLinearColor Panel(0.02f, 0.05f, 0.1f, 0.75f);
	const FLinearColor Gold(1.0f, 0.85f, 0.2f);

	if (Sessions->IsSessionActive())
	{
		// One row at the top centre, above the jump readout and the trick card: the clock, then the
		// counting slots.
		const FBestThreeSession& Session = Sessions->GetSession();
		const float Scale = 1.25f * UiScale;
		const float Gap = 28.0f * UiScale;
		TArray<float> Widths;
		float RowW = 0.0f;
		float RowH = 0.0f;
		for (const FString& Line : Lines)
		{
			float W = 0.0f;
			float H = 0.0f;
			GetTextSize(Line, W, H, nullptr, Scale);
			Widths.Add(W);
			RowW += W;
			RowH = FMath::Max(RowH, H);
		}
		RowW += Gap * (Lines.Num() - 1);
		float X = ScreenW * 0.5f - RowW * 0.5f;
		const float Y = Margin;
		const float Pad = 10.0f * UiScale;
		DrawRect(Panel, X - Pad * 1.6f, Y - Pad * 0.6f, RowW + Pad * 3.2f, RowH + Pad * 1.2f);
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			FLinearColor Ink = FLinearColor(0.6f, 1.0f, 0.7f);
			if (Index == 0)
			{
				const bool bHurry = Session.GetPhase() == EBestThreePhase::Overtime || Session.GetTimeLeft() <= 10.0f;
				Ink = bHurry ? FLinearColor(1.0f, 0.55f, 0.2f) : Gold;
			}
			else if (Lines[Index].EndsWith(TEXT("--")))
			{
				Ink = FLinearColor(0.6f, 0.65f, 0.7f);
			}
			DrawText(Lines[Index], Ink, X, Y, nullptr, Scale);
			X += Widths[Index] + Gap;
		}
		return;
	}

	// The results card, centred: title, total, the NEW BEST badge, the counting jumps, the local best.
	struct FStyledLine { FString Text; FLinearColor Ink; float Scale; float W = 0.0f; float H = 0.0f; };
	TArray<FStyledLine> Styled;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const FString& Line = Lines[Index];
		FStyledLine& Out = Styled.AddDefaulted_GetRef();
		Out.Text = Line;
		if (Index == 0)
		{
			Out.Ink = Gold;
			Out.Scale = 1.4f;
		}
		else if (Index == 1)
		{
			Out.Ink = FLinearColor::White;
			Out.Scale = 2.2f;
		}
		else if (Line == TEXT("NEW BEST"))
		{
			Out.Ink = Gold;
			Out.Scale = 1.7f;
		}
		else if (Index == Lines.Num() - 1)
		{
			Out.Ink = FLinearColor(0.55f, 0.9f, 1.0f);
			Out.Scale = 1.15f;
		}
		else
		{
			Out.Ink = FLinearColor(0.85f, 0.9f, 0.95f);
			Out.Scale = 1.2f;
		}
		Out.Scale *= UiScale;
		GetTextSize(Out.Text, Out.W, Out.H, nullptr, Out.Scale);
	}
	const float Spacing = 8.0f * UiScale;
	float CardW = 0.0f;
	float CardH = 0.0f;
	for (const FStyledLine& Line : Styled)
	{
		CardW = FMath::Max(CardW, Line.W);
		CardH += Line.H + Spacing;
	}
	CardH -= Spacing;
	const float Pad = 24.0f * UiScale;
	const float Top = FMath::Max(ScreenH * 0.3f, Margin + Pad);
	DrawRect(FLinearColor(0.01f, 0.03f, 0.08f, 0.85f), ScreenW * 0.5f - CardW * 0.5f - Pad, Top - Pad, CardW + 2.0f * Pad, CardH + 2.0f * Pad);
	DrawRect(FLinearColor(Gold.R, Gold.G, Gold.B, 0.9f), ScreenW * 0.5f - CardW * 0.5f - Pad, Top - Pad, CardW + 2.0f * Pad, 3.0f * UiScale);
	float Y = Top;
	for (const FStyledLine& Line : Styled)
	{
		DrawText(Line.Text, Line.Ink, ScreenW * 0.5f - Line.W * 0.5f, Y, nullptr, Line.Scale);
		Y += Line.H + Spacing;
	}
}

TArray<FString> AKiteSurfHUD::FormatHeatRow(const FFreestyleHeat& Heat)
{
	TArray<FString> Row;
	Row.Add(TEXT("FREESTYLE HEAT"));
	Row.Add(FString::Printf(TEXT("Trick %d/%d"), Heat.GetCurrentTrickNumber(), Heat.GetAttemptLimit()));
	if (Heat.IsCountdownOn())
	{
		Row.Add(FormatSessionClock(Heat.GetCountdownLeft()));
	}
	const FHeatResult& Result = Heat.GetResult();
	FString Total = FString::Printf(TEXT("TOTAL %.1f"), Result.Total);
	if (Result.VarietyBonus > 0.0f)
	{
		Total += FString::Printf(TEXT(" (+%s variety)"), *FreestyleHeat::FormatBonus(Result.VarietyBonus));
	}
	Row.Add(Total);
	return Row;
}

TArray<FString> AKiteSurfHUD::FormatHeatList(const FFreestyleHeat& Heat)
{
	TArray<FString> Lines;
	Lines.Add(TEXT("COUNTING"));
	const TArray<FScoredTrick> Counting = Heat.GetCounting();
	const int32 Slots = FMath::Max(Heat.Settings.Rules.Counting, 0);
	for (int32 Slot = 0; Slot < Slots; ++Slot)
	{
		if (Counting.IsValidIndex(Slot))
		{
			const FScoredTrick& Trick = Counting[Slot];
			Lines.Add(FString::Printf(TEXT("%d  %s  %.1f  [%s]"), Slot + 1, Trick.Name.IsEmpty() ? TEXT("Trick") : *Trick.Name, Trick.Score,
				*FreestyleHeat::FamilyLabel(Trick.Family)));
		}
		else
		{
			Lines.Add(FString::Printf(TEXT("%d  --"), Slot + 1));
		}
	}
	return Lines;
}

TArray<FString> AKiteSurfHUD::FormatHeatResults(const FFreestyleHeat& Heat, float PreviousBest, bool bHadPreviousBest, bool bNewBest)
{
	TArray<FString> Lines;
	const FHeatResult& Result = Heat.GetResult();
	const int32 Attempts = Heat.GetAttemptLimit();
	Lines.Add(FString::Printf(TEXT("FREESTYLE HEAT OVER  (%d %s)"), Attempts, Attempts == 1 ? TEXT("trick") : TEXT("tricks")));
	Lines.Add(FString::Printf(TEXT("TOTAL  %.1f"), Result.Total));
	if (bNewBest)
	{
		Lines.Add(TEXT("NEW BEST"));
	}
	const TArray<FScoredTrick> Counting = Heat.GetCounting();
	for (int32 Rank = 0; Rank < Counting.Num(); ++Rank)
	{
		const FScoredTrick& Trick = Counting[Rank];
		Lines.Add(FString::Printf(TEXT("%d  %s  %.1f pts  %s"), Rank + 1, Trick.Name.IsEmpty() ? TEXT("Trick") : *Trick.Name, Trick.Score,
			*FreestyleHeat::FamilyLabel(Trick.Family)));
	}
	if (Counting.Num() == 0)
	{
		Lines.Add(TEXT("No tricks counted"));
	}
	const TArray<EGkaFamily> Families = Heat.GetFamiliesUsed();
	TArray<FString> Labels;
	for (const EGkaFamily Family : Families)
	{
		Labels.Add(FreestyleHeat::FamilyLabel(Family));
	}
	Lines.Add(Families.Num() > 0 ? FString::Printf(TEXT("Families  %d: %s"), Families.Num(), *FString::Join(Labels, TEXT(", ")))
		: FString(TEXT("Families  0")));
	Lines.Add(FString::Printf(TEXT("Variety bonus  +%s"), *FreestyleHeat::FormatBonus(Result.VarietyBonus)));
	if (!bHadPreviousBest)
	{
		Lines.Add(FString::Printf(TEXT("First %d-trick heat"), Attempts));
	}
	else
	{
		Lines.Add(FString::Printf(TEXT("%s  %.1f"), bNewBest ? TEXT("Previous best") : TEXT("Local best"), PreviousBest));
	}
	return Lines;
}

TArray<FString> AKiteSurfHUD::GetHeatLines() const
{
	const UWorld* World = GetWorld();
	const UFreestyleHeatSubsystem* Heats = World ? World->GetSubsystem<UFreestyleHeatSubsystem>() : nullptr;
	if (!Heats)
	{
		return TArray<FString>();
	}
	if (Heats->IsHeatActive())
	{
		TArray<FString> Lines = FormatHeatRow(Heats->GetHeat());
		Lines.Append(FormatHeatList(Heats->GetHeat()));
		return Lines;
	}
	if (Heats->IsShowingResults())
	{
		return FormatHeatResults(Heats->GetHeat(), Heats->GetPreviousBest(), Heats->HadPreviousBest(), Heats->IsNewBest());
	}
	return TArray<FString>();
}

void AKiteSurfHUD::UpdateHeatNotice()
{
	const UWorld* World = GetWorld();
	const UFreestyleHeatSubsystem* Heats = World ? World->GetSubsystem<UFreestyleHeatSubsystem>() : nullptr;
	if (!Heats || Heats->GetNoticeSerial() == SeenHeatNoticeSerial)
	{
		return;
	}
	SeenHeatNoticeSerial = Heats->GetNoticeSerial();
	ShowNotice(Heats->GetNotice());
}

void AKiteSurfHUD::DrawHeat(float ScreenW, float ScreenH)
{
	const UWorld* World = GetWorld();
	const UFreestyleHeatSubsystem* Heats = World ? World->GetSubsystem<UFreestyleHeatSubsystem>() : nullptr;
	if (!Heats || (!Heats->IsHeatActive() && !Heats->IsShowingResults()))
	{
		return;
	}
	// Sized and placed from the screen height inside a safe-area margin, as the session panel.
	const float UiScale = FMath::Clamp(ScreenH / 1080.0f, 0.85f, 1.6f);
	const float Margin = FMath::Max(16.0f, ScreenH * 0.02f);
	const FLinearColor Panel(0.02f, 0.05f, 0.1f, 0.75f);
	const FLinearColor Gold(1.0f, 0.85f, 0.2f);
	const FLinearColor Grey(0.6f, 0.65f, 0.7f);
	const FFreestyleHeat& Heat = Heats->GetHeat();

	if (Heats->IsHeatActive())
	{
		// The row at the top centre: the title, "Trick 3/7", the countdown and the total.
		const TArray<FString> Row = FormatHeatRow(Heat);
		const float Scale = 1.25f * UiScale;
		const float Gap = 28.0f * UiScale;
		TArray<float> Widths;
		float RowW = 0.0f;
		float RowH = 0.0f;
		for (const FString& Item : Row)
		{
			float W = 0.0f;
			float H = 0.0f;
			GetTextSize(Item, W, H, nullptr, Scale);
			Widths.Add(W);
			RowW += W;
			RowH = FMath::Max(RowH, H);
		}
		RowW += Gap * (Row.Num() - 1);
		float X = ScreenW * 0.5f - RowW * 0.5f;
		const float Y = Margin;
		const float Pad = 10.0f * UiScale;
		DrawRect(Panel, X - Pad * 1.6f, Y - Pad * 0.6f, RowW + Pad * 3.2f, RowH + Pad * 1.2f);
		for (int32 Index = 0; Index < Row.Num(); ++Index)
		{
			FLinearColor Ink = FLinearColor(0.6f, 1.0f, 0.7f);
			if (Index == 0)
			{
				Ink = Gold;
			}
			else if (Index == 1)
			{
				Ink = FLinearColor::White;
			}
			else if (Heat.IsCountdownOn() && Index == 2)
			{
				Ink = Heat.GetCountdownLeft() <= 10.0f ? FLinearColor(1.0f, 0.55f, 0.2f) : FLinearColor(0.55f, 0.9f, 1.0f);
			}
			DrawText(Row[Index], Ink, X, Y, nullptr, Scale);
			X += Widths[Index] + Gap;
		}

		// The counting list at the right edge, under the wind flag and above the power gauge.
		const TArray<FString> List = FormatHeatList(Heat);
		const float ListScale = 1.05f * UiScale;
		float ListW = 0.0f;
		float ListH = 0.0f;
		TArray<float> Heights;
		for (const FString& Line : List)
		{
			float W = 0.0f;
			float H = 0.0f;
			GetTextSize(Line, W, H, nullptr, ListScale);
			ListW = FMath::Max(ListW, W);
			Heights.Add(H);
			ListH += H + 4.0f * UiScale;
		}
		const float ListPad = 10.0f * UiScale;
		const float Left = ScreenW - Margin - ListW - 2.0f * ListPad;
		// Clear of the wind flag's panel (drawn at a fixed 64 px, about 195 px tall).
		const float Top = FMath::Max(272.0f, ScreenH * 0.26f);
		DrawRect(Panel, Left, Top, ListW + 2.0f * ListPad, ListH + 2.0f * ListPad);
		float LineY = Top + ListPad;
		for (int32 Index = 0; Index < List.Num(); ++Index)
		{
			const FLinearColor Ink = Index == 0 ? Gold : (List[Index].EndsWith(TEXT("--")) ? Grey : FLinearColor(0.85f, 0.9f, 0.95f));
			DrawText(List[Index], Ink, Left + ListPad, LineY, nullptr, ListScale);
			LineY += Heights[Index] + 4.0f * UiScale;
		}
		return;
	}

	// The results card, centred: title, total, NEW BEST, the counting tricks, families, bonus, the local best.
	const TArray<FString> Lines = FormatHeatResults(Heat, Heats->GetPreviousBest(), Heats->HadPreviousBest(), Heats->IsNewBest());
	struct FStyledLine { FString Text; FLinearColor Ink; float Scale = 1.0f; float W = 0.0f; float H = 0.0f; };
	TArray<FStyledLine> Styled;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const FString& Line = Lines[Index];
		FStyledLine& Out = Styled.AddDefaulted_GetRef();
		Out.Text = Line;
		if (Index == 0)
		{
			Out.Ink = Gold;
			Out.Scale = 1.4f;
		}
		else if (Index == 1)
		{
			Out.Ink = FLinearColor::White;
			Out.Scale = 2.2f;
		}
		else if (Line == TEXT("NEW BEST"))
		{
			Out.Ink = Gold;
			Out.Scale = 1.7f;
		}
		else if (Line.StartsWith(TEXT("Families")) || Line.StartsWith(TEXT("Variety bonus")))
		{
			Out.Ink = FLinearColor(0.6f, 1.0f, 0.7f);
			Out.Scale = 1.15f;
		}
		else if (Index == Lines.Num() - 1)
		{
			Out.Ink = FLinearColor(0.55f, 0.9f, 1.0f);
			Out.Scale = 1.15f;
		}
		else
		{
			Out.Ink = FLinearColor(0.85f, 0.9f, 0.95f);
			Out.Scale = 1.2f;
		}
		Out.Scale *= UiScale;
		GetTextSize(Out.Text, Out.W, Out.H, nullptr, Out.Scale);
	}
	const float Spacing = 8.0f * UiScale;
	float CardW = 0.0f;
	float CardH = 0.0f;
	for (const FStyledLine& Line : Styled)
	{
		CardW = FMath::Max(CardW, Line.W);
		CardH += Line.H + Spacing;
	}
	CardH -= Spacing;
	const float Pad = 24.0f * UiScale;
	// Where the session's card goes: below the jump readout, the trick card and its NEW BEST line.
	const float Top = FMath::Max(ScreenH * 0.3f, Margin + Pad);
	DrawRect(FLinearColor(0.01f, 0.03f, 0.08f, 0.85f), ScreenW * 0.5f - CardW * 0.5f - Pad, Top - Pad, CardW + 2.0f * Pad, CardH + 2.0f * Pad);
	DrawRect(FLinearColor(Gold.R, Gold.G, Gold.B, 0.9f), ScreenW * 0.5f - CardW * 0.5f - Pad, Top - Pad, CardW + 2.0f * Pad, 3.0f * UiScale);
	float Y = Top;
	for (const FStyledLine& Line : Styled)
	{
		DrawText(Line.Text, Line.Ink, ScreenW * 0.5f - Line.W * 0.5f, Y, nullptr, Line.Scale);
		Y += Line.H + Spacing;
	}
}

ALessonDirector* AKiteSurfHUD::FindLessonDirector() const
{
	if (ALessonDirector* Cached = CachedLessonDirector.Get())
	{
		if (Cached->IsRunning())
		{
			return Cached;
		}
	}
	CachedLessonDirector.Reset();
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ALessonDirector> It(World); It; ++It)
	{
		if (It->IsRunning())
		{
			CachedLessonDirector = *It;
			return *It;
		}
	}
	return nullptr;
}

void AKiteSurfHUD::UpdateLessonLayer(float DeltaTime)
{
	// The world's time dilation (a lesson's slow motion) scaled DeltaTime; the drop-back hold is
	// timed in real seconds.
	float Dilation = 1.0f;
	if (const UWorld* World = GetWorld())
	{
		if (const AWorldSettings* Settings = World->GetWorldSettings())
		{
			Dilation = Settings->GetEffectiveTimeDilation();
		}
	}
	const float RealDeltaTime = Dilation > KINDA_SMALL_NUMBER ? DeltaTime / Dilation : DeltaTime;
	ALessonDirector* Director = FindLessonDirector();
	LessonLayer.Update(Director, DeltaTime, RealDeltaTime);
	if (Director && LessonLayer.ConsumeDropBackHold() && Director->IsDropBackOffered())
	{
		UE_LOG(LogKiteSchool, Display, TEXT("HUD: reset held %.1f s on the drop-back offer: dropping back"), LessonHUD::DropBackHoldSeconds);
		Director->AcceptDropBack();
		LessonLayer.Update(FindLessonDirector(), 0.0f, 0.0f);
	}
}

void AKiteSurfHUD::SetLessonResetHeld(bool bHeld)
{
	LessonLayer.SetResetHeld(bHeld);
}

bool AKiteSurfHUD::HandleLessonAction(ELessonHUDAction Action)
{
	ALessonDirector* Director = FindLessonDirector();
	if (!Director)
	{
		return false;
	}
	const bool bCard = Director->GetPhase() == ELessonPhase::Result
		&& (Director->GetOutcome() == ELessonOutcome::Passed || Director->GetOutcome() == ELessonOutcome::Failed);
	switch (Action)
	{
	case ELessonHUDAction::Confirm:
		return bCard && Director->GetOutcome() == ELessonOutcome::Passed && Director->Next();
	case ELessonHUDAction::Retry:
		// Off the card a press is the rider's reset; the drop-back offer needs it held (SetLessonResetHeld).
		return bCard && Director->Retry();
	case ELessonHUDAction::Menu:
		if (bCard)
		{
			ULessonSubsystem* Lessons = Director->GetLessonSubsystem();
			return Lessons && Lessons->RequestLessonMenu();
		}
		return false;
	default:
		return false;
	}
}

void AKiteSurfHUD::BindLessonInput(AKiteRiderPawn* RiderPawn)
{
	UEnhancedInputComponent* Input = RiderPawn ? Cast<UEnhancedInputComponent>(RiderPawn->InputComponent) : nullptr;
	if (!Input || LessonInputComponent.Get() == Input)
	{
		return;
	}
	LessonInputComponent = Input;
	// The rider's own actions: no new input assets. Its handlers still run; these only act while the
	// result card or the drop-back offer is up.
	if (UInputAction* Jump = RiderPawn->GetJumpAction())
	{
		Input->BindAction(Jump, ETriggerEvent::Started, this, &AKiteSurfHUD::OnLessonJumpInput);
	}
	if (UInputAction* Reset = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/IA_Reset.IA_Reset")))
	{
		Input->BindAction(Reset, ETriggerEvent::Started, this, &AKiteSurfHUD::OnLessonResetInput);
		// IA_Reset has no triggers: Completed when let go (Canceled for safety).
		Input->BindAction(Reset, ETriggerEvent::Completed, this, &AKiteSurfHUD::OnLessonResetReleased);
		Input->BindAction(Reset, ETriggerEvent::Canceled, this, &AKiteSurfHUD::OnLessonResetReleased);
	}
}

void AKiteSurfHUD::OnLessonJumpInput()
{
	HandleLessonAction(ELessonHUDAction::Confirm);
}

void AKiteSurfHUD::OnLessonResetInput()
{
	// The result card's Retry on a press; otherwise the start of a possible hold on the drop-back offer.
	if (!HandleLessonAction(ELessonHUDAction::Retry))
	{
		SetLessonResetHeld(true);
	}
}

void AKiteSurfHUD::OnLessonResetReleased()
{
	SetLessonResetHeld(false);
}
