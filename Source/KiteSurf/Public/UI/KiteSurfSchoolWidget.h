#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "School/LessonDirector.h"
#include "School/LessonSubsystem.h"
#include "School/LessonTypes.h"
#include "Styling/SlateBrush.h"
#include "UI/KiteSurfMenuNavigator.h"
#include "KiteSurfSchoolWidget.generated.h"

class SButton;
class STextBlock;
class UTexture2D;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSchoolClosed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSchoolLessonStarted, FName, LessonId);

/** A chapter of the kite school as the chapter map shows it (docs/tutorials.md section 2). */
struct FSchoolChapterInfo
{
	/** "A" to "F", as FLessonDef::Chapter. */
	FName Id;
	FText Name;
	/** The chapter's wind, e.g. "10-14 kn". */
	FText Wind;
};

/** The words the lesson menu shows, as pure functions so tests can read them. */
namespace SchoolMenuText
{
	/** The six chapters, A to F, in order; some have no lessons in the catalogue yet. */
	KITESURF_API const TArray<FSchoolChapterInfo>& GetChapters();

	/** What an objective asks for in a line, e.g. "Jump 1-2 m, 5 in a row, landed Clean or better". */
	KITESURF_API FText DescribeObjective(const FLessonObjective& Objective);

	/** A value of a metric in its unit, e.g. "1.6 m", "Clean", "14 kn". */
	KITESURF_API FText FormatValue(ELessonMetric Metric, float Value);

	/** When a lesson was last played: "today", "yesterday", "3 days ago", or the date. */
	KITESURF_API FText FormatLastPlayed(const FDateTime& PlayedUtc, const FDateTime& NowUtc);

	/** The assists that are on, e.g. "auto-park, auto-edge"; "none" when every one is off. */
	KITESURF_API FText ListAssists(const FLessonAssists& Assists);

	/** What a feature not built yet is called, e.g. "grabs". */
	KITESURF_API FText FeatureName(ELessonFeature Feature);
}

/**
 * The kite school's lesson menu (docs/tutorials.md 3.1, S5): a chapter map of lesson tiles (stars,
 * locked, new, coming soon), the focused lesson's details (what it teaches, what it needs, what
 * passes it, the player's best), Start with the wind and assists for a rerun, Continue to the
 * recommended lesson, the overall progress, and Reset progress behind a confirm step (the trick
 * book is never reset).
 *
 * Opened from the main menu's SCHOOL (full screen over the menu art), from the pause menu's SCHOOL
 * or LESSON MENU during a ride (over the paused game, bDuringRide), and by
 * ULessonSubsystem::RequestLessonMenu. Built in Slate like the gear and settings screens; the
 * widget keeps keyboard focus and routes keys through FKiteMenuNavigator. The navigator's items,
 * top to bottom: CONTINUE, one row per chapter that has lessons (left and right move along its
 * tiles, accept starts the focused lesson), WIND, ASSISTS, START, RESET PROGRESS, BACK. While the
 * reset asks for confirmation the items are RESET and CANCEL only.
 */
UCLASS()
class KITESURF_API UKiteSurfSchoolWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UKiteSurfSchoolWidget(const FObjectInitializer& ObjectInitializer);

	/** BACK or Escape. */
	UPROPERTY(BlueprintAssignable, Category = "School")
	FOnSchoolClosed OnClosedDelegate;

	/** A lesson was started (it is pending or already running); the menu has closed itself. */
	UPROPERTY(BlueprintAssignable, Category = "School")
	FOnSchoolLessonStarted OnLessonStartedDelegate;

	/** Opened over a paused ride: the game is dimmed behind it instead of the menu art. */
	UPROPERTY(BlueprintReadWrite, Category = "School")
	bool bDuringRide = false;

	/** Uses this subsystem instead of the game instance's (tests: one that never writes to disk). */
	void SetLessonSubsystem(ULessonSubsystem* InLessons);

	ULessonSubsystem* GetLessons() const;

	/** Reads the lesson list again (after a result or a reset). */
	UFUNCTION(BlueprintCallable, Category = "School")
	void Refresh();

	/** The tiles: every catalogue lesson in chapter and number order. */
	const TArray<FLessonListItem>& GetTiles() const { return Tiles; }

	const FLessonListItem* FindTile(FName LessonId) const;

	/** The badge on a tile: "COMING SOON", "LOCKED", "NEW" or empty. */
	FText GetTileBadge(FName LessonId) const;

	/** The lesson the detail panel shows and Start starts. */
	UFUNCTION(BlueprintPure, Category = "School")
	FName GetFocusedLessonId() const { return FocusedLessonId; }

	/** Shows a lesson in the detail panel; the rerun choices go back to the lesson's own. */
	UFUNCTION(BlueprintCallable, Category = "School")
	void FocusLesson(FName LessonId);

	/** Starts the focused lesson with the chosen wind and assists. False (nothing changes) when it is locked or coming soon. */
	UFUNCTION(BlueprintCallable, Category = "School")
	bool StartFocusedLesson();

	/** The lesson Continue starts (ULessonSubsystem::GetRecommendedNext). */
	UFUNCTION(BlueprintPure, Category = "School")
	FName GetContinueLessonId() const;

	/** Starts the recommended lesson as the lesson sets it. */
	UFUNCTION(BlueprintCallable, Category = "School")
	bool Continue();

	// --- Rerun choices for the focused lesson. ---

	/** The wind the run will have (kn): the lesson's, or more. */
	UFUNCTION(BlueprintPure, Category = "School")
	float GetRunWindKnots() const { return RunWindKnots; }

	/** Clamped to the lesson's wind up to MaxExtraWindKnots more. */
	UFUNCTION(BlueprintCallable, Category = "School")
	void SetRunWindKnots(float Knots);

	/** How far above the lesson's wind a rerun may go (kn). */
	static constexpr float MaxExtraWindKnots = 10.0f;

	/** The assist choices: the lesson's own, each of its assists left off, then all off. */
	int32 GetAssistChoiceCount() const;
	int32 GetAssistChoice() const { return AssistChoice; }
	void StepAssistChoice(int32 Direction);
	FText GetAssistChoiceText() const;

	/** What a start passes to the subsystem: the chosen wind and assists. */
	FLessonRunOptions GetRunOptions() const;

	// --- Reset progress. ---

	/** RESET PROGRESS: asks for confirmation; nothing is reset yet. */
	UFUNCTION(BlueprintCallable, Category = "School")
	void RequestReset();

	/** Forgets every lesson result (ULessonSubsystem::ResetProgress). The trick book stays. */
	UFUNCTION(BlueprintCallable, Category = "School")
	void ConfirmReset();

	UFUNCTION(BlueprintCallable, Category = "School")
	void CancelReset();

	UFUNCTION(BlueprintPure, Category = "School")
	bool IsConfirmingReset() const { return bConfirmingReset; }

	/** BACK: broadcasts OnClosed and leaves. */
	UFUNCTION(BlueprintCallable, Category = "School")
	void Close();

	UFUNCTION(BlueprintCallable, Category = "School")
	void FocusFirst();

	/** Keyboard and gamepad navigation. Public so tests can drive it. */
	FKiteMenuNavigator& GetNavigator();

	/** Navigator indices of the items (INDEX_NONE when not there). */
	int32 GetContinueItem() const { return ContinueItem; }
	int32 GetChapterItem(FName Chapter) const;
	int32 GetWindItem() const { return WindItem; }
	int32 GetAssistsItem() const { return AssistsItem; }
	int32 GetStartItem() const { return StartItem; }
	int32 GetResetItem() const { return ResetItem; }
	int32 GetBackItem() const { return BackItem; }
	int32 GetConfirmResetItem() const { return ConfirmResetItem; }
	int32 GetCancelResetItem() const { return CancelResetItem; }

	// --- What the panels show. ---

	/** "STARS 5 / 39   TRICK BOOK 3". */
	FText GetOverallProgressText() const;
	/** A chapter's header figure, e.g. "57%"; empty for a chapter with no lessons. */
	FText GetChapterProgressText(FName Chapter) const;
	/** The focused lesson's state: "NEW", "LOCKED: pass A4 Upwind first", "PASSED: 2 stars" ... */
	FText GetDetailStatusText() const;
	FText GetDetailTeachText() const;
	FText GetDetailNeedsText() const;
	FText GetDetailPassText() const;
	FText GetDetailBestText() const;
	/** Whether the focused lesson can be started now. */
	bool CanStartFocused() const;
	/** Watch demo appears once a lesson has a demonstration (S11); none has yet. */
	bool IsWatchDemoVisible() const;

protected:
	virtual void NativeConstruct() override;
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void BuildNavigation();
	FKiteMenuNavigator::FItem MakeChapterItem(FName Chapter);
	/** Lessons of a chapter, in order. */
	TArray<FName> ChapterLessons(FName Chapter) const;
	const FLessonDef* GetFocusedLesson() const;
	bool StartLessonWith(FName LessonId, const FLessonRunOptions& Options);
	void SelectItem(int32 Index);

	TSharedRef<SWidget> BuildMapPanel();
	TSharedRef<SWidget> BuildDetailPanel();
	TSharedRef<SWidget> BuildChapter(const FSchoolChapterInfo& Chapter);
	TSharedRef<SWidget> BuildTile(FName LessonId, FName Chapter);
	TSharedRef<SWidget> BuildConfirmPanel();
	TSharedRef<SWidget> MakeButton(TSharedPtr<SButton>& OutButton, TAttribute<FText> Label, TFunction<void()> OnClicked, int32 FontSize = 16);

	UPROPERTY(Transient)
	TObjectPtr<ULessonSubsystem> LessonsOverride;

	TArray<FLessonListItem> Tiles;
	FName FocusedLessonId;
	float RunWindKnots = 0.0f;
	int32 AssistChoice = 0;
	bool bConfirmingReset = false;
	bool bRefreshed = false;

	/** The chapter row the navigator has selected, None when another item is (the rows draw their own highlight). */
	FName HighlightedChapter;
	/** The tile column carried from row to row as the selection moves up and down. */
	int32 Column = 0;

	FKiteMenuNavigator Navigator;
	int32 ContinueItem = INDEX_NONE;
	TMap<FName, int32> ChapterItems;
	int32 WindItem = INDEX_NONE;
	int32 AssistsItem = INDEX_NONE;
	int32 StartItem = INDEX_NONE;
	int32 DemoItem = INDEX_NONE;
	int32 ResetItem = INDEX_NONE;
	int32 BackItem = INDEX_NONE;
	int32 ConfirmResetItem = INDEX_NONE;
	int32 CancelResetItem = INDEX_NONE;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> BackgroundTexture;
	FSlateBrush BackgroundBrush;

	TSharedPtr<SButton> ContinueButton;
	TSharedPtr<SButton> WindButton;
	TSharedPtr<SButton> AssistsButton;
	TSharedPtr<SButton> StartButton;
	TSharedPtr<SButton> DemoButton;
	TSharedPtr<SButton> ResetButton;
	TSharedPtr<SButton> BackButton;
	TSharedPtr<SButton> ConfirmResetButton;
	TSharedPtr<SButton> CancelResetButton;
};
