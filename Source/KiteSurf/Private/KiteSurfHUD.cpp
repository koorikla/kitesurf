#include "KiteSurfHUD.h"
#include "KiteRiderPawn.h"
#include "BoardMovementComponent.h"
#include "KiteComponent.h"
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

AKiteSurfHUD::AKiteSurfHUD()
	: CurrentOnboardingStep(0)
	, bOnboardingActive(true)
	, CurrentStepProgress(0.0f)
	, bHasInitializedOnboarding(false)
	, PromptAlpha(1.0f)
	, StepCompletionTimer(0.0f)
{
}

void AKiteSurfHUD::TogglePauseMenu()
{
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
	return SpeedCmPerSec / 51.44f;
}

float AKiteSurfHUD::KnotsToCmPerSec(float Knots)
{
	return Knots * 51.44f;
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

void AKiteSurfHUD::StartOnboarding()
{
	CurrentOnboardingStep = 0;
	bOnboardingActive = true;
	CurrentStepProgress = 0.0f;
	StepCompletionTimer = 0.0f;
	PromptAlpha = 1.0f;
}

void AKiteSurfHUD::SkipOnboarding()
{
	bOnboardingActive = false;
	CurrentOnboardingStep = 4;
	if (UWorld* World = GetWorld())
	{
		if (UKiteSurfGameInstance* GI = Cast<UKiteSurfGameInstance>(World->GetGameInstance()))
		{
			GI->SetSkipOnboarding(true);
			GI->SaveSettingsToDisk();
		}
	}
}

void AKiteSurfHUD::AdvanceOnboardingStep()
{
	CurrentOnboardingStep++;
	CurrentStepProgress = 0.0f;
	StepCompletionTimer = 0.0f;
	if (CurrentOnboardingStep >= 4)
	{
		bOnboardingActive = false;
		if (UWorld* World = GetWorld())
		{
			if (UKiteSurfGameInstance* GI = Cast<UKiteSurfGameInstance>(World->GetGameInstance()))
			{
				GI->SetOnboardingCompleted(true);
				GI->SaveSettingsToDisk();
			}
		}
	}
}

FString AKiteSurfHUD::GetCurrentPromptText() const
{
	switch (CurrentOnboardingStep)
	{
	case 0:
		return TEXT("Steer the kite: fly it up and over to the other side to turn around [A / D or Left Stick]");
	case 1:
		return TEXT("Sheet in for power, out to slow down - the bar stays where you leave it [W / S or Triggers]");
	case 2:
		return TEXT("Carve the board: hold an edge to turn upwind or downwind [Q / E or Left Stick up / down]");
	case 3:
		return TEXT("Send it: hold an edge and pop off the water to jump [SPACE or Bottom Face Button]");
	default:
		return TEXT("TUTORIAL COMPLETE - ENJOY THE OPEN WATER!");
	}
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

	AKiteRiderPawn* RiderPawn = Cast<AKiteRiderPawn>(GetOwningPawn());
	if (RiderPawn)
	{
		DrawTelemetry(RiderPawn);
		DrawWindWindowArc(RiderPawn, ScreenW * 0.5f, ScreenH - 50.0f, 110.0f);
		DrawPowerGauge(RiderPawn, ScreenW - 200.0f, ScreenH - 250.0f, 40.0f, 200.0f);
		UpdateOnboarding(DeltaTime, RiderPawn);
	}

	if (bOnboardingActive)
	{
		DrawOnboardingPrompt(ScreenW, ScreenH);
	}

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

	DrawRect(FLinearColor(0.02f, 0.05f, 0.1f, 0.65f), 20.0f, 20.0f, 280.0f, 204.0f);
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
	// Sampled at the wind field's reference height: at the water the shear profile reads 30% low.
	const FVector WindSampleLocation = RiderPawn->GetActorLocation() + FVector(0.0f, 0.0f, WindComp ? WindComp->ShearHeightCm : 0.0f);
	FVector WindVec = WindComp ? WindComp->GetWindAt(WindSampleLocation) : FVector(772.0f, 0.0f, 0.0f);
	FString WindStr = FString::Printf(TEXT("WIND:  %s"), *FormatKnots(WindVec.Size()));
	DrawText(WindStr, FLinearColor(0.3f, 0.8f, 1.0f), 32.0f, 94.0f, nullptr, 1.1f);

	DrawWindCompass(WindVec, 255.0f, 96.0f, 18.0f);

	if (const UBoardMovementComponent* BoardMove = RiderPawn->GetBoardMovement())
	{
		const EBoardState BoardState = BoardMove->GetBoardState();
		FString StateStr = TEXT("STATE: Displacement");
		if (BoardState == EBoardState::Planing)
		{
			StateStr = TEXT("STATE: Planing");
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

		FString JumpStr = FString::Printf(TEXT("JUMP: Best %.1fm | Apex %.1fm"),
			BoardMove->GetBestJumpHeight() / 100.0f,
			BoardMove->GetLastJumpApexHeight() / 100.0f);
		DrawText(JumpStr, FLinearColor(0.85f, 0.95f, 1.0f), 32.0f, 144.0f, nullptr, 1.1f);
	}

	const FString SheetStr = FString::Printf(TEXT("BAR: %.0f%% sheeted in"), RiderPawn->GetCurrentSheetInput() * 100.0f);
	DrawText(SheetStr, FLinearColor(0.85f, 0.95f, 1.0f), 32.0f, 168.0f, nullptr, 1.1f);
}

void AKiteSurfHUD::DrawWindCompass(const FVector& WindVec, float CenterX, float CenterY, float Radius)
{
	FVector2D Dir(WindVec.X, -WindVec.Y);
	Dir.Normalize();
	if (Dir.IsNearlyZero())
	{
		Dir = FVector2D(1.0f, 0.0f);
	}

	FVector2D Tip = FVector2D(CenterX, CenterY) + Dir * Radius;
	FVector2D Tail = FVector2D(CenterX, CenterY) - Dir * (Radius * 0.7f);
	DrawLine(Tail.X, Tail.Y, Tip.X, Tip.Y, FLinearColor(0.3f, 0.85f, 1.0f), 2.5f);

	FVector2D Perp(-Dir.Y, Dir.X);
	FVector2D Wing1 = Tip - Dir * (Radius * 0.45f) + Perp * (Radius * 0.35f);
	FVector2D Wing2 = Tip - Dir * (Radius * 0.45f) - Perp * (Radius * 0.35f);
	DrawLine(Tip.X, Tip.Y, Wing1.X, Wing1.Y, FLinearColor(0.3f, 0.85f, 1.0f), 2.5f);
	DrawLine(Tip.X, Tip.Y, Wing2.X, Wing2.Y, FLinearColor(0.3f, 0.85f, 1.0f), 2.5f);
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
}

void AKiteSurfHUD::DrawFPS(float ScreenX, float ScreenY)
{
	float DeltaTime = GetWorld()->GetDeltaSeconds();
	float FPS = DeltaTime > 0.0f ? (1.0f / DeltaTime) : 0.0f;
	FString FPSText = FString::Printf(TEXT("FPS: %.0f"), FPS);
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

void AKiteSurfHUD::UpdateOnboarding(float DeltaTime, AKiteRiderPawn* RiderPawn)
{
	if (!bHasInitializedOnboarding)
	{
		bHasInitializedOnboarding = true;
		if (UWorld* World = GetWorld())
		{
			if (UKiteSurfGameInstance* GI = Cast<UKiteSurfGameInstance>(World->GetGameInstance()))
			{
				if (GI->bSkipOnboarding || GI->bOnboardingCompleted)
				{
					bOnboardingActive = false;
					CurrentOnboardingStep = 4;
					return;
				}
			}
		}
	}

	if (!bOnboardingActive || !RiderPawn)
	{
		return;
	}

	switch (CurrentOnboardingStep)
	{
	case 0: // Steer
		{
			const float SteerInput = FMath::Abs(RiderPawn->GetCurrentSteerInput());
			if (SteerInput > 0.25f)
			{
				CurrentStepProgress += DeltaTime * 0.75f;
			}
			if (CurrentStepProgress >= 1.0f)
			{
				StepCompletionTimer += DeltaTime;
				if (StepCompletionTimer >= 0.8f)
				{
					AdvanceOnboardingStep();
				}
			}
			break;
		}
	case 1: // Sheet
		{
			// The bar holds its position, so progress comes from moving it, not from where it sits.
			const float SheetInput = FMath::Abs(RiderPawn->GetSheetRateInput());
			if (SheetInput > 0.3f)
			{
				CurrentStepProgress += DeltaTime * 0.75f;
			}
			if (CurrentStepProgress >= 1.0f)
			{
				StepCompletionTimer += DeltaTime;
				if (StepCompletionTimer >= 0.8f)
				{
					AdvanceOnboardingStep();
				}
			}
			break;
		}
	case 2: // Edge
		{
			if (const UBoardMovementComponent* BoardMove = RiderPawn->GetBoardMovement())
			{
				if (FMath::Abs(BoardMove->GetEdgeInput()) > 0.2f)
				{
					CurrentStepProgress += DeltaTime * 0.75f;
				}
			}
			if (CurrentStepProgress >= 1.0f)
			{
				StepCompletionTimer += DeltaTime;
				if (StepCompletionTimer >= 0.8f)
				{
					AdvanceOnboardingStep();
				}
			}
			break;
		}
	case 3: // Jump
		{
			if (const UBoardMovementComponent* BoardMove = RiderPawn->GetBoardMovement())
			{
				if (BoardMove->GetBoardState() == EBoardState::Airborne || BoardMove->GetLastJumpApexHeight() > 10.0f)
				{
					CurrentStepProgress = 1.0f;
					StepCompletionTimer += DeltaTime;
					if (StepCompletionTimer >= 1.2f)
					{
						AdvanceOnboardingStep();
					}
				}
			}
			break;
		}
	default:
		break;
	}
}

void AKiteSurfHUD::DrawOnboardingPrompt(float ScreenW, float ScreenH)
{
	const FString PromptText = GetCurrentPromptText();
	float TextW = 0.0f;
	float TextH = 0.0f;
	GetTextSize(PromptText, TextW, TextH, nullptr, 1.25f);

	const float CenterX = ScreenW * 0.5f;
	const float PromptY = 90.0f;
	const float BoxW = FMath::Max(TextW + 40.0f, 480.0f);
	const float BoxH = 75.0f;
	const float BoxX = CenterX - (BoxW * 0.5f);

	// Background container
	DrawRect(FLinearColor(0.02f, 0.06f, 0.12f, 0.85f), BoxX, PromptY, BoxW, BoxH);
	// Top accent line
	DrawRect(FLinearColor(0.2f, 0.8f, 1.0f, 0.9f), BoxX, PromptY, BoxW, 3.0f);

	// Step indicator (e.g. "STEP 1/4")
	FString StepHeader = FString::Printf(TEXT("ONBOARDING - STEP %d OF 4"), FMath::Min(CurrentOnboardingStep + 1, 4));
	if (CurrentOnboardingStep >= 4)
	{
		StepHeader = TEXT("ONBOARDING COMPLETE");
	}
	DrawText(StepHeader, FLinearColor(0.2f, 0.85f, 1.0f), BoxX + 16.0f, PromptY + 10.0f, nullptr, 0.9f);

	// Prompt instruction text
	const FLinearColor TextColor = (CurrentStepProgress >= 1.0f) ? FLinearColor(0.3f, 1.0f, 0.4f) : FLinearColor::White;
	DrawText(PromptText, TextColor, BoxX + 16.0f, PromptY + 30.0f, nullptr, 1.15f);

	// Progress bar at bottom of card
	const float BarW = BoxW - 32.0f;
	const float BarH = 6.0f;
	const float BarX = BoxX + 16.0f;
	const float BarY = PromptY + BoxH - 14.0f;
	DrawRect(FLinearColor(0.15f, 0.2f, 0.25f, 0.9f), BarX, BarY, BarW, BarH);
	const float ClampedProgress = FMath::Clamp(CurrentStepProgress, 0.0f, 1.0f);
	DrawRect(FLinearColor(0.2f, 0.85f, 1.0f, 1.0f), BarX, BarY, BarW * ClampedProgress, BarH);
}
