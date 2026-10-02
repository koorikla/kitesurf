#include "KiteSurfHUD.h"
#include "KiteRiderPawn.h"
#include "WindComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"
#include "UI/KiteSurfPauseMenuWidget.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/UserWidget.h"

AKiteSurfHUD::AKiteSurfHUD()
{
}

void AKiteSurfHUD::TogglePauseMenu()
{
	if (ActivePauseMenuWidget && ActivePauseMenuWidget->IsInViewport())
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
	ActivePauseMenuWidget = CreateWidget<UKiteSurfPauseMenuWidget>(PC, ClassToSpawn);
	if (!ActivePauseMenuWidget)
	{
		return;
	}
	ActivePauseMenuWidget->AddToViewport(100);
	UGameplayStatics::SetGamePaused(World, true);
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
		ActivePauseMenuWidget->OnResumeClicked();
		ActivePauseMenuWidget = nullptr;
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

void AKiteSurfHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	const float ScreenW = Canvas->ClipX;
	const float ScreenH = Canvas->ClipY;

	DrawFPS(ScreenW - 130.0f, 25.0f);

	AKiteRiderPawn* RiderPawn = Cast<AKiteRiderPawn>(GetOwningPawn());
	if (RiderPawn)
	{
		DrawTelemetry(RiderPawn);
		DrawWindWindowArc(RiderPawn, ScreenW * 0.5f, ScreenH - 50.0f, 110.0f);
	}
}

void AKiteSurfHUD::DrawTelemetry(AKiteRiderPawn* RiderPawn)
{
	if (!RiderPawn)
	{
		return;
	}

	DrawRect(FLinearColor(0.02f, 0.05f, 0.1f, 0.65f), 20.0f, 20.0f, 280.0f, 145.0f);
	DrawText(TEXT("KITESURF TELEMETRY"), FLinearColor(1.0f, 0.85f, 0.2f), 32.0f, 28.0f, nullptr, 1.1f);

	FVector Vel = RiderPawn->GetBoardVelocity();
	FString SpeedStr = FString::Printf(TEXT("SPEED: %s"), *FormatKnots(Vel.Size2D()));
	DrawText(SpeedStr, FLinearColor::White, 32.0f, 52.0f, nullptr, 1.2f);

	float HeadingDeg = FRotator::NormalizeAxis(RiderPawn->GetActorRotation().Yaw);
	if (HeadingDeg < 0.0f)
	{
		HeadingDeg += 360.0f;
	}
	FString HeadingStr = FString::Printf(TEXT("HEADING: %.0f deg"), HeadingDeg);
	DrawText(HeadingStr, FLinearColor(0.85f, 0.95f, 1.0f), 32.0f, 76.0f, nullptr, 1.2f);

	UWindComponent* WindComp = RiderPawn->FindComponentByClass<UWindComponent>();
	FVector WindVec = WindComp ? WindComp->GetWindAt(RiderPawn->GetActorLocation()) : FVector(772.0f, 0.0f, 0.0f);
	FString WindStr = FString::Printf(TEXT("WIND:  %s"), *FormatKnots(WindVec.Size()));
	DrawText(WindStr, FLinearColor(0.3f, 0.8f, 1.0f), 32.0f, 102.0f, nullptr, 1.2f);

	DrawWindCompass(WindVec, 255.0f, 108.0f, 18.0f);
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
	if (!RiderPawn)
	{
		return;
	}

	DrawRect(FLinearColor(0.02f, 0.05f, 0.1f, 0.65f), CenterX - 160.0f, CenterY - 145.0f, 320.0f, 160.0f);

	float AzimuthDeg = RiderPawn->GetKiteAzimuthDeg();
	FString AzimuthStr = FString::Printf(TEXT("WIND WINDOW (AZIMUTH: %+.1f deg)"), AzimuthDeg);
	DrawText(AzimuthStr, FLinearColor(1.0f, 0.85f, 0.2f), CenterX - 140.0f, CenterY - 135.0f, nullptr, 1.0f);

	const int32 NumSegments = 30;
	for (int32 i = 0; i < NumSegments; ++i)
	{
		float Deg1 = -90.0f + (180.0f * i) / NumSegments;
		float Deg2 = -90.0f + (180.0f * (i + 1)) / NumSegments;
		float Rad1 = FMath::DegreesToRadians(270.0f + Deg1);
		float Rad2 = FMath::DegreesToRadians(270.0f + Deg2);
		float X1 = CenterX + Radius * FMath::Cos(Rad1);
		float Y1 = CenterY + Radius * FMath::Sin(Rad1);
		float X2 = CenterX + Radius * FMath::Cos(Rad2);
		float Y2 = CenterY + Radius * FMath::Sin(Rad2);
		DrawLine(X1, Y1, X2, Y2, FLinearColor(0.4f, 0.7f, 1.0f, 0.8f), 2.0f);
	}

	const float Ticks[] = { -90.0f, -45.0f, 0.0f, 45.0f, 90.0f };
	for (float TickDeg : Ticks)
	{
		float Rad = FMath::DegreesToRadians(270.0f + TickDeg);
		float XInner = CenterX + (Radius - 8.0f) * FMath::Cos(Rad);
		float YInner = CenterY + (Radius - 8.0f) * FMath::Sin(Rad);
		float XOuter = CenterX + (Radius + 8.0f) * FMath::Cos(Rad);
		float YOuter = CenterY + (Radius + 8.0f) * FMath::Sin(Rad);
		DrawLine(XInner, YInner, XOuter, YOuter, FLinearColor(0.8f, 0.9f, 1.0f), 1.5f);
	}

	DrawText(TEXT("-90"), FLinearColor::White, CenterX - Radius - 15.0f, CenterY - 15.0f, nullptr, 0.8f);
	DrawText(TEXT("0"), FLinearColor::White, CenterX - 4.0f, CenterY - Radius - 20.0f, nullptr, 0.8f);
	DrawText(TEXT("+90"), FLinearColor::White, CenterX + Radius - 10.0f, CenterY - 15.0f, nullptr, 0.8f);

	float KiteRad = FMath::DegreesToRadians(270.0f + AzimuthDeg);
	float MarkerX = CenterX + Radius * FMath::Cos(KiteRad);
	float MarkerY = CenterY + Radius * FMath::Sin(KiteRad);
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
