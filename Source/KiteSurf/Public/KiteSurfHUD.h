#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "BoardMovementComponent.h"
#include "Tricks/TrickTypes.h"
#include "School/LessonHUD.h"
#include "KiteSurfHUD.generated.h"

class AKiteRiderPawn;
class UTrickTrackerComponent;
class FBestThreeSession;
class ALessonDirector;
class UInputComponent;
struct FJumpRecord;

UCLASS()
class KITESURF_API AKiteSurfHUD : public AHUD
{
	GENERATED_BODY()

public:
	AKiteSurfHUD();

	virtual void DrawHUD() override;

	UFUNCTION(BlueprintCallable, Category = "UI|Jump")
	void ShowJumpRejection(EJumpRejectReason Reason);

	/** Shows a short message in the same place for a couple of seconds ("Ran aground", "Shark!"). */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void ShowNotice(const FString& Text);

	UFUNCTION(BlueprintPure, Category = "UI|Jump")
	FString GetJumpRejectionText() const { return JumpRejectionRemainingTime > 0.0f ? JumpRejectionText : FString(); }

	UFUNCTION(BlueprintPure, Category = "UI|Jump")
	float GetJumpRejectionRemainingTime() const { return JumpRejectionRemainingTime; }

	/**
	 * The landing card's text for a landing of this load (g): "LANDED 4.2 g", with "HOT" after it for a
	 * hot landing (the rider sank fast or the kite was low), and "CRASH" instead of "LANDED" for a crash.
	 */
	UFUNCTION(BlueprintPure, Category = "UI|Jump")
	static FString FormatLandingCard(float LandingG, bool bHot, bool bClean);

	/**
	 * Shows the landing card for LandingCardSeconds when the board has landed since the last call
	 * (UBoardMovementComponent::GetLandingCount), and counts it down by DeltaTime. DrawHUD calls it with
	 * the rider's board every frame.
	 */
	void UpdateLandingCard(const UBoardMovementComponent* Board, float DeltaTime);

	UFUNCTION(BlueprintPure, Category = "UI|Jump")
	FString GetLandingCardText() const { return LandingCardRemainingTime > 0.0f ? LandingCardText : FString(); }

	UFUNCTION(BlueprintPure, Category = "UI|Jump")
	bool IsLandingCardHot() const { return LandingCardRemainingTime > 0.0f && bLandingCardHot; }

	/** How long the landing card stays up after a landing (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Jump", meta = (ClampMin = "0.0"))
	float LandingCardSeconds = 3.0f;

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

	/** Converts cm/s to knots (KiteUnits::CmPerKnot) */
	UFUNCTION(BlueprintPure, Category = "KiteSurf|HUD")
	static float CmPerSecToKnots(float SpeedCmPerSec);

	/** Converts knots to cm/s (KiteUnits::CmPerKnot) */
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

	/** A jump in progress, e.g. "12.4 m high   35 m far   2.1 s". */
	static FString FormatJumpLive(float HeightCm, float DistanceCm, float AirSeconds);

	/** A finished jump, e.g. "JUMP  14.8 m high   62 m far   4.1 s". */
	static FString FormatJumpResult(float ApexCm, float DistanceCm, float AirSeconds);

	/**
	 * Follows the board's jumps: the height and distance so far while the rider is in the air,
	 * then the finished jump's figures for a few seconds after it. Hops under a metre are ignored.
	 */
	void UpdateJumpReadout(const class UBoardMovementComponent* Board, float DeltaTime);

	/** What the jump readout shows now; empty when there is nothing to show. */
	UFUNCTION(BlueprintPure, Category = "UI|Jump")
	FString GetJumpReadoutText() const { return JumpReadoutText; }

	/** Whether the jump being shown beat the session's best height. */
	UFUNCTION(BlueprintPure, Category = "UI|Jump")
	bool IsJumpReadoutNewBest() const { return bJumpReadoutNewBest; }

	/**
	 * The trick card for a finished jump (T0.5), two lines:
	 * "<Name>  <GRADE>  <N> pts" and "<g> g landing", e.g. "Kiteloop  CLEAN  41 pts\n3.2 g landing".
	 * The points are what the session paid (Score.Total x RepeatFactor), rounded; a repeat paid less
	 * than in full adds "  (repeat NN%)" to the first line.
	 * The grade is the board's landing verdict's (the record's Grade). A record whose verdict named a
	 * cause adds a third line, the cause's LandingCauseLine (T2.6): "Back roll  CRASH  0 pts\n5.1 g
	 * landing\nUnder-rotated: commit the roll earlier". The verdict names one only for a sketchy
	 * landing or a crash, so a stomped or clean card has two lines.
	 */
	static FString FormatJumpCard(const FJumpRecord& Record);

	/**
	 * The one-line failure message for a landing cause (T2.6), e.g. "Under-rotated: commit the roll
	 * earlier" or "Kite too low at touchdown"; empty for None.
	 */
	static FString LandingCauseLine(ELandingCause Cause);

	/** The card's cause line (its third line), empty when the card has none or is not up. */
	UFUNCTION(BlueprintPure, Category = "UI|Jump")
	FString GetJumpCardCauseText() const;

	/** STOMPED, CLEAN, SKETCHY or CRASH. */
	static FString GradeText(ELandingGrade Grade);

	/** Green, white, amber or red (the red of the rejection notice). */
	static FLinearColor GradeColor(ELandingGrade Grade);

	/**
	 * Follows the trick tracker: when its record count goes up after a jump of at least a metre
	 * (the jump readout's threshold), shows that jump's card for a few seconds; while a jump is in
	 * the air and has an element such as a completed kite loop, names it live in the ticker.
	 * Polls the record count, as UpdateJumpReadout polls the board's jump count.
	 */
	void UpdateJumpCard(const UTrickTrackerComponent* Tracker, float DeltaTime);

	/** Shows a record's card now (and clears the ticker). UpdateJumpCard calls it; tests can too. */
	void ShowJumpCard(const FJumpRecord& Record);

	/**
	 * Names a jump in progress for the ticker; empty while it has no element (loop, rotation, grab...).
	 * Rotations appear as the recogniser credits them: "Back roll", then "Double back roll".
	 */
	static FString FormatTrickTicker(const FJumpRecord& LiveJump);

	/** What the trick card shows now; empty when there is nothing to show. */
	UFUNCTION(BlueprintPure, Category = "UI|Jump")
	FString GetJumpCardText() const { return JumpCardText; }

	/** The grade of the card being shown. */
	UFUNCTION(BlueprintPure, Category = "UI|Jump")
	ELandingGrade GetJumpCardGrade() const { return JumpCardGrade; }

	/** The live trick name while in the air; empty when there is nothing to name. */
	UFUNCTION(BlueprintPure, Category = "UI|Jump")
	FString GetTrickTickerText() const { return TickerText; }

	/** A session clock: seconds left rounded up, as "1:30", "0:09" or "0:00". */
	static FString FormatSessionClock(float SecondsLeft);

	/**
	 * One counting jump in the session panel (T2.5): its points to 1 dp, with " x0.75" when the
	 * session paid it as a repeat. Only the score, so the row stays narrow; the results card names
	 * the jumps.
	 */
	static FString FormatSessionSlot(const FJumpRecord& Counting);

	/**
	 * The session panel while a best-three session runs: the clock ("SESSION 1:12", or
	 * "SESSION 0:00  OVERTIME" while a jump that took off before the horn is in the air), then
	 * one line per counting slot, "1  <slot>" or "1  --" while it is empty.
	 */
	static TArray<FString> FormatSessionPanel(const FBestThreeSession& Session);

	/**
	 * The results card: "SESSION OVER  (90 s)", "TOTAL  <total to 1 dp>", "NEW BEST" when it
	 * beat the local best, one line per counting jump ("1  <Name>  <height> m  <points> pts") or
	 * "No jumps counted", then the local best: "Local best  <best>" when it was not beaten,
	 * "Previous best  <best>" when it was, "First <N> s session" when there was none.
	 */
	static TArray<FString> FormatSessionResults(const FBestThreeSession& Session, float PreviousBest, bool bHadPreviousBest, bool bNewBest);

	/**
	 * What the session part of the HUD shows now, from the world's UTrickSessionSubsystem: the
	 * panel while a session runs, the results card while it is up, otherwise nothing. DrawHUD draws
	 * these lines; tests read them with no Canvas.
	 */
	TArray<FString> GetSessionLines() const;

	// --- Kite school lesson layer (docs/tutorials.md S4). ---

	/** The running lesson director in this HUD's world, or null. */
	ALessonDirector* FindLessonDirector() const;

	/**
	 * Reads the running lesson's director into the lesson layer and moves its timers on (DeltaTime is
	 * game time; the drop-back hold runs in real time, undoing the world's time dilation), and takes
	 * the drop-back offer once the reset button has been held for LessonHUD::DropBackHoldSeconds.
	 * DrawHUD calls it every frame; tests call it with no Canvas and read GetLessonView.
	 */
	void UpdateLessonLayer(float DeltaTime);

	/**
	 * The reset button went down or up (the HUD's own IA_Reset Started and Completed bindings; tests
	 * call it). Held from while the drop-back offer shows, it takes the offer after
	 * LessonHUD::DropBackHoldSeconds; a tap only resets the rider, as the rider's own handler does.
	 */
	void SetLessonResetHeld(bool bHeld);

	/** What the lesson layer shows now: the panel, the cue lines, the result card. Invisible with no lesson running. */
	const FLessonHUDView& GetLessonView() const { return LessonLayer.GetView(); }

	/** Whether a lesson is up. */
	bool IsLessonLayerVisible() const { return LessonLayer.IsVisible(); }

	// --- First-run tutorial (docs/tutorials.md S7): lessons A1 to A3 replaced the old four-step onboarding prompt. ---

	/**
	 * Reads the tutorial line for the running lesson from the game instance's
	 * USchoolOnboardingSubsystem (GetHintLines): the welcome and how to skip during A1 to A3, the
	 * "tutorial complete" choices on A3's result card. DrawHUD calls it; tests call it with no Canvas.
	 */
	void UpdateTutorialHint();

	/** What the tutorial line shows now; empty outside the tutorial. */
	const TArray<FString>& GetTutorialHintLines() const { return TutorialHintLines; }

	/**
	 * The result card's actions, from the existing inputs: the jump button (Confirm) goes to the next
	 * lesson after a pass; the reset button (Retry) retries from the result card (the drop-back offer
	 * needs the button held: SetLessonResetHeld); the pause button (Menu) on the result card asks
	 * ULessonSubsystem::RequestLessonMenu, and opens the pause menu as usual when nothing answers.
	 * True when the action did something.
	 */
	bool HandleLessonAction(ELessonHUDAction Action);

protected:
	FLessonHUDLayer LessonLayer;
	mutable TWeakObjectPtr<ALessonDirector> CachedLessonDirector;
	/** The rider's input component the lesson actions are bound on. */
	TWeakObjectPtr<UInputComponent> LessonInputComponent;
	/** Binds the lesson actions to the rider's existing jump and reset actions, beside the rider's own handlers. */
	void BindLessonInput(AKiteRiderPawn* RiderPawn);
	void OnLessonJumpInput();
	void OnLessonResetInput();
	void OnLessonResetReleased();

	/** Draws GetSessionLines: the panel as one row at the top centre, above the jump readout and trick card; the results card in the middle. */
	void DrawSession(float ScreenW, float ScreenH);

	UPROPERTY(Transient, BlueprintReadOnly, Category = "UI|Jump")
	FString JumpRejectionText;

	FString JumpReadoutText;
	float JumpResultRemainingTime = 0.0f;
	int32 SeenJumpCount = 0;
	float BestHeightBeforeJumpCm = 0.0f;
	bool bJumpReadoutNewBest = false;
	bool bJumpReadoutLive = false;

	FString JumpCardText;
	FString TickerText;
	ELandingGrade JumpCardGrade = ELandingGrade::Clean;
	float JumpCardRemainingTime = 0.0f;
	int32 SeenRecordCount = 0;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "UI|Jump")
	float JumpRejectionRemainingTime = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "UI|Jump")
	FString LandingCardText;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "UI|Jump")
	float LandingCardRemainingTime = 0.0f;

	bool bLandingCardHot = false;
	bool bLandingCardClean = true;
	/** The board's landing count when the card last looked; -1 before it has seen the board. */
	int32 SeenLandingCount = -1;

	void DrawLandingCard(float ScreenW, float ScreenH);

	void DrawTelemetry(AKiteRiderPawn* RiderPawn);
	void DrawWindWindowArc(AKiteRiderPawn* RiderPawn, float CenterX, float CenterY, float Radius);
	void DrawWindFlag(AKiteRiderPawn* RiderPawn, float ScreenX, float ScreenY, float Size);
	void DrawFPS(float ScreenX, float ScreenY);
	void DrawPowerGauge(AKiteRiderPawn* RiderPawn, float ScreenX, float ScreenY, float Width, float Height);
	void DrawControlBar(AKiteRiderPawn* RiderPawn, float ScreenX, float ScreenY, float Width, float Height);

	TArray<FString> TutorialHintLines;
	/** Draws GetTutorialHintLines in a box at the top centre, from Top; returns the box's bottom (Top when there is nothing). */
	float DrawTutorialHint(float ScreenW, float ScreenH, float Top);
};
