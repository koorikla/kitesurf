#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "BoardMovementComponent.h"
#include "KiteSurfHUD.generated.h"

class AKiteRiderPawn;

UCLASS()
class KITESURF_API AKiteSurfHUD : public AHUD
{
	GENERATED_BODY()

public:
	AKiteSurfHUD();

	virtual void DrawHUD() override;

	/** Onboarding Tutorial Steps */
	UPROPERTY(BlueprintReadOnly, Category = "UI|Onboarding")
	int32 CurrentOnboardingStep; // 0=Steer, 1=Sheet, 2=Edge, 3=Jump, 4=Completed

	UPROPERTY(BlueprintReadOnly, Category = "UI|Onboarding")
	bool bOnboardingActive;

	UPROPERTY(BlueprintReadOnly, Category = "UI|Onboarding")
	float CurrentStepProgress;

	UFUNCTION(BlueprintPure, Category = "UI|Onboarding")
	bool IsOnboardingActive() const { return bOnboardingActive; }

	UFUNCTION(BlueprintPure, Category = "UI|Onboarding")
	int32 GetCurrentOnboardingStep() const { return CurrentOnboardingStep; }

	UFUNCTION(BlueprintPure, Category = "UI|Onboarding")
	float GetCurrentStepProgress() const { return CurrentStepProgress; }

	UFUNCTION(BlueprintCallable, Category = "UI|Onboarding")
	void StartOnboarding();

	UFUNCTION(BlueprintCallable, Category = "UI|Onboarding")
	void SkipOnboarding();

	UFUNCTION(BlueprintCallable, Category = "UI|Onboarding")
	void AdvanceOnboardingStep();

	UFUNCTION(BlueprintPure, Category = "UI|Onboarding")
	FString GetCurrentPromptText() const;

	UFUNCTION(BlueprintCallable, Category = "UI|Jump")
	void ShowJumpRejection(EJumpRejectReason Reason);

	/** Shows a short message in the same place for a couple of seconds ("Ran aground", "Shark!"). */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void ShowNotice(const FString& Text);

	UFUNCTION(BlueprintPure, Category = "UI|Jump")
	FString GetJumpRejectionText() const { return JumpRejectionRemainingTime > 0.0f ? JumpRejectionText : FString(); }

	UFUNCTION(BlueprintPure, Category = "UI|Jump")
	float GetJumpRejectionRemainingTime() const { return JumpRejectionRemainingTime; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	TSubclassOf<class UKiteSurfPauseMenuWidget> PauseMenuWidgetClass;

	UFUNCTION(BlueprintCallable, Category = "UI")
	void TogglePauseMenu();

	UFUNCTION(BlueprintCallable, Category = "UI")
	void ShowPauseMenu();

	UFUNCTION(BlueprintCallable, Category = "UI")
	void HidePauseMenu();

	UPROPERTY(Transient, BlueprintReadOnly, Category = "UI")
	TObjectPtr<class UKiteSurfPauseMenuWidget> ActivePauseMenuWidget;

	UFUNCTION(BlueprintCallable, Category = "UI")
	UKiteSurfPauseMenuWidget* GetActivePauseMenuWidget() const { return ActivePauseMenuWidget; }

	/** Converts velocity in cm/s to formatted knots string, e.g. "15.0 kn" */
	UFUNCTION(BlueprintPure, Category = "KiteSurf|HUD")
	static FString FormatKnots(float SpeedCmPerSec, bool bIncludeUnit = true);

	/** Converts cm/s to knots (1 kn = 51.44 cm/s) */
	UFUNCTION(BlueprintPure, Category = "KiteSurf|HUD")
	static float CmPerSecToKnots(float SpeedCmPerSec);

	/** Converts knots to cm/s (1 kn = 51.44 cm/s) */
	UFUNCTION(BlueprintPure, Category = "KiteSurf|HUD")
	static float KnotsToCmPerSec(float Knots);

	/**
	 * Which way the wind blows as seen on screen, for a view looking along CameraYawDeg: a unit
	 * vector with x to the right and y down the screen, so wind from behind the camera points up.
	 * Zero in a calm.
	 */
	static FVector2D GetWindOnScreen(const FVector& Wind, float CameraYawDeg);

	/** Where the wind comes from relative to the view, in words: "from behind", "from the left", ... */
	static FString DescribeWindSource(const FVector2D& WindOnScreen);

	/**
	 * Where the two ends of the control bar are drawn. The bar slides down the throw as it is
	 * pulled in (Sheet 0 at ThrowTop, 1 at ThrowTop + ThrowLength) and the end of the hand that
	 * is pulling drops towards the rider: steer right lowers the right end.
	 */
	static void GetBarEnds(float Steer, float Sheet, const FVector2D& ThrowTop, float ThrowLength, float HalfWidth, float MaxTiltDeg, FVector2D& OutLeftEnd, FVector2D& OutRightEnd);

protected:
	UPROPERTY(Transient, BlueprintReadOnly, Category = "UI|Jump")
	FString JumpRejectionText;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "UI|Jump")
	float JumpRejectionRemainingTime = 0.0f;

	void DrawTelemetry(AKiteRiderPawn* RiderPawn);
	void DrawWindWindowArc(AKiteRiderPawn* RiderPawn, float CenterX, float CenterY, float Radius);
	void DrawWindFlag(AKiteRiderPawn* RiderPawn, float ScreenX, float ScreenY, float Size);
	void DrawFPS(float ScreenX, float ScreenY);
	void DrawPowerGauge(AKiteRiderPawn* RiderPawn, float ScreenX, float ScreenY, float Width, float Height);
	void DrawControlBar(AKiteRiderPawn* RiderPawn, float ScreenX, float ScreenY, float Width, float Height);
	void DrawOnboardingPrompt(float ScreenW, float ScreenH);
	void UpdateOnboarding(float DeltaTime, AKiteRiderPawn* RiderPawn);

	bool bHasInitializedOnboarding;
	float PromptAlpha;
	float StepCompletionTimer;
};
