#include "UI/KiteSurfSchoolWidget.h"
#include "KiteGear.h"
#include "School/LessonCatalog.h"
#include "School/LessonProgress.h"
#include "School/LessonTelemetry.h"
#include "Tricks/TrickTypes.h"
#include "UI/KiteSurfGameInstance.h"
#include "UI/KiteSurfMenuStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Reply.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "KiteSurfSchool"

namespace KiteSurfSchoolPrivate
{
	const FLinearColor SchoolTitleColor(1.0f, 0.85f, 0.2f);
	const FLinearColor SchoolLabelColor(0.75f, 0.82f, 0.9f);
	const FLinearColor SchoolHintColor(0.55f, 0.75f, 0.9f);
	const FLinearColor SchoolSelectedColor(1.0f, 0.8f, 0.15f);
	const FLinearColor SchoolDimColor(0.45f, 0.5f, 0.56f);
	const FLinearColor SchoolNewColor(1.0f, 0.55f, 0.15f);
	const FLinearColor SchoolStarOnColor(1.0f, 0.8f, 0.15f);
	const FLinearColor SchoolStarOffColor(0.18f, 0.22f, 0.28f);

	constexpr float SchoolUnbounded = UE_BIG_NUMBER * 0.5f;
	constexpr float SchoolMSPerKnot = 0.5144f;
	constexpr int32 SchoolAssistCount = 6;

	bool SchoolHasMin(float Min) { return Min > -SchoolUnbounded; }
	bool SchoolHasMax(float Max) { return Max < SchoolUnbounded; }

	/** A number with no more than two decimals and no trailing zeros: 1, 0.5, 0.15. */
	FString SchoolNum(float Value)
	{
		FString S = FString::Printf(TEXT("%.2f"), Value);
		if (S.Contains(TEXT(".")))
		{
			while (S.EndsWith(TEXT("0")))
			{
				S.LeftChopInline(1);
			}
			if (S.EndsWith(TEXT(".")))
			{
				S.LeftChopInline(1);
			}
		}
		return S == TEXT("-0") ? TEXT("0") : S;
	}

	/** "1-2 m", "0.5 m or more", "4 s or less". Min and Max in the metric's unit; Scale converts for display. */
	FString SchoolRange(float Min, float Max, const FString& Unit, float Scale = 1.0f)
	{
		const bool bMin = SchoolHasMin(Min) && !(SchoolHasMax(Max) && Min <= 0.0f);
		const bool bMax = SchoolHasMax(Max);
		if (bMin && bMax)
		{
			return FString::Printf(TEXT("%s–%s%s"), *SchoolNum(Min * Scale), *SchoolNum(Max * Scale), *Unit);
		}
		if (bMin)
		{
			return FString::Printf(TEXT("%s%s or more"), *SchoolNum(Min * Scale), *Unit);
		}
		if (bMax)
		{
			return FString::Printf(TEXT("%s%s or less"), *SchoolNum(Max * Scale), *Unit);
		}
		return FString();
	}

	/** The display unit of a metric and the factor from its stored unit. */
	FString SchoolUnitOf(ELessonMetric Metric, float& OutScale)
	{
		OutScale = 1.0f;
		switch (Metric)
		{
		case ELessonMetric::JumpHeight:
		case ELessonMetric::JumpDistance:
		case ELessonMetric::UpwindGain:
		case ELessonMetric::DistanceRidden:
			return TEXT(" m");
		case ELessonMetric::Airtime:
		case ELessonMetric::LoopStartSinceApex:
		case ELessonMetric::LoopDuration:
		case ELessonMetric::GrabHoldSeconds:
		case ELessonMetric::DiveBeforeTouchdown:
		case ELessonMetric::TimeToPlaning:
		case ELessonMetric::TransitionNotPlaningSeconds:
		case ELessonMetric::KiteLeadAtTransition:
			return TEXT(" s");
		case ELessonMetric::TakeoffSpeed:
		case ELessonMetric::SpeedHeld:
			OutScale = 1.0f / SchoolMSPerKnot;
			return TEXT(" kn");
		case ELessonMetric::SinkAtTouchdown:
			return TEXT(" m/s");
		case ELessonMetric::LandingG:
			return TEXT(" g");
		case ELessonMetric::KiteElevationAtTouchdown:
		case ELessonMetric::MinKiteElevationInAir:
		case ELessonMetric::RotationDeg:
		case ELessonMetric::HeadingChange:
		case ELessonMetric::KiteElevationHeld:
			return TEXT("°");
		case ELessonMetric::TransitionSpeedKept:
			OutScale = 100.0f;
			return TEXT("%");
		default:
			return FString();
		}
	}

	FString SchoolChannelName(ELessonChannel Channel)
	{
		return StaticEnum<ELessonChannel>()->GetDisplayNameTextByValue(static_cast<int64>(Channel)).ToString().ToLower();
	}

	FString SchoolGradeName(float Index)
	{
		const int32 Grade = FMath::Clamp(FMath::RoundToInt(Index), 0, static_cast<int32>(ELandingGrade::Crash));
		return StaticEnum<ELandingGrade>()->GetDisplayNameTextByValue(Grade).ToString();
	}

	bool SchoolIsHeld(ELessonMetric Metric)
	{
		return Metric == ELessonMetric::SpeedHeld || Metric == ELessonMetric::KiteElevationHeld || Metric == ELessonMetric::ChannelHeld;
	}

	bool& SchoolAssistFlag(FLessonAssists& Assists, int32 Index)
	{
		switch (Index)
		{
		case 0: return Assists.bAutoPark;
		case 1: return Assists.bAutoEdge;
		case 2: return Assists.bLandingAssist;
		case 3: return Assists.bAutoRedirect;
		case 4: return Assists.bLoopCatch;
		default: return Assists.bSlowMotion;
		}
	}

	const TCHAR* SchoolAssistName(int32 Index)
	{
		static const TCHAR* Names[SchoolAssistCount] = { TEXT("auto-park"), TEXT("auto-edge"), TEXT("landing assist"), TEXT("auto-redirect"), TEXT("loop catch"), TEXT("slow motion") };
		return Names[FMath::Clamp(Index, 0, SchoolAssistCount - 1)];
	}

	/** The assists the lesson has on, by index. */
	TArray<int32> SchoolAssistsOn(const FLessonAssists& Assists)
	{
		FLessonAssists Copy = Assists;
		TArray<int32> On;
		for (int32 I = 0; I < SchoolAssistCount; ++I)
		{
			if (SchoolAssistFlag(Copy, I))
			{
				On.Add(I);
			}
		}
		return On;
	}

	/** The lesson list against an empty progress book, for a menu with no subsystem. */
	TArray<FLessonListItem> SchoolFreshList()
	{
		const FLessonProgressBook Empty;
		TArray<FLessonListItem> Items;
		for (const FLessonDef& Lesson : LessonCatalog::GetAll())
		{
			FLessonListItem& Item = Items.AddDefaulted_GetRef();
			Item.LessonId = Lesson.Id;
			Item.Title = Lesson.Title;
			Item.Chapter = Lesson.Chapter;
			Item.bAvailable = LessonCatalog::IsAvailable(Lesson);
			Item.bLocked = !LessonUnlock::IsUnlocked(Lesson, Empty);
			Item.bNew = !Item.bLocked;
		}
		return Items;
	}

	FString SchoolLessonLabel(FName LessonId)
	{
		const FLessonDef* Lesson = LessonCatalog::Find(LessonId);
		return Lesson ? FString::Printf(TEXT("%s %s"), *Lesson->Id.ToString(), *Lesson->Title.ToString()) : LessonId.ToString();
	}
}

// ---------------------------------------------------------------------------------------------
// SchoolMenuText
// ---------------------------------------------------------------------------------------------

const TArray<FSchoolChapterInfo>& SchoolMenuText::GetChapters()
{
	static const TArray<FSchoolChapterInfo> Chapters = {
		{ TEXT("A"), LOCTEXT("Chapter.A", "RIDING"), LOCTEXT("Chapter.A.Wind", "10–14 kn") },
		{ TEXT("B"), LOCTEXT("Chapter.B", "JUMPS"), LOCTEXT("Chapter.B.Wind", "12–16 kn") },
		{ TEXT("C"), LOCTEXT("Chapter.C", "ROTATIONS"), LOCTEXT("Chapter.C.Wind", "12–16 kn") },
		{ TEXT("D"), LOCTEXT("Chapter.D", "BIG AIR"), LOCTEXT("Chapter.D.Wind", "16–22 kn") },
		{ TEXT("E"), LOCTEXT("Chapter.E", "KITE LOOPS"), LOCTEXT("Chapter.E.Wind", "10–20 kn") },
		{ TEXT("F"), LOCTEXT("Chapter.F", "UNHOOKED"), LOCTEXT("Chapter.F.Wind", "14–18 kn") },
	};
	return Chapters;
}

FText SchoolMenuText::DescribeObjective(const FLessonObjective& O)
{
	using namespace KiteSurfSchoolPrivate;
	float Scale = 1.0f;
	const FString Unit = SchoolUnitOf(O.Metric, Scale);
	const FString R = SchoolRange(O.Min, O.Max, Unit, Scale);
	FString S;
	switch (O.Metric)
	{
	case ELessonMetric::JumpHeight: S = TEXT("Jump ") + R; break;
	case ELessonMetric::Airtime: S = TEXT("Stay in the air ") + R; break;
	case ELessonMetric::JumpDistance: S = TEXT("Jump ") + R + TEXT(" far"); break;
	case ELessonMetric::TakeoffSpeed: S = TEXT("Take off at ") + R; break;
	case ELessonMetric::LandingGrade: S = TEXT("Land ") + SchoolGradeName(O.Max) + TEXT(" or better"); break;
	case ELessonMetric::SinkAtTouchdown: S = TEXT("Touch down sinking ") + R; break;
	case ELessonMetric::LandingG: S = TEXT("Land at ") + R; break;
	case ELessonMetric::KiteElevationAtTouchdown: S = TEXT("Kite at ") + R + TEXT(" at touchdown"); break;
	case ELessonMetric::MinKiteElevationInAir: S = TEXT("Kite at ") + R + TEXT(" all through the air"); break;
	case ELessonMetric::Popped: S = TEXT("Pop off the water"); break;
	case ELessonMetric::CompletedLoops: S = TEXT("Complete ") + R + TEXT(" kite loops"); break;
	case ELessonMetric::LoopKind: S = TEXT("Fly a ") + StaticEnum<ETrickLoopKind>()->GetDisplayNameTextByValue(static_cast<int64>(O.LoopKind)).ToString().ToLower(); break;
	case ELessonMetric::LoopStartSinceApex: S = TEXT("Start the loop ") + R + TEXT(" from the apex"); break;
	case ELessonMetric::LoopDuration: S = TEXT("Loop the kite in ") + R; break;
	case ELessonMetric::TrickNameContains: S = TEXT("Land a ") + O.Text; break;
	case ELessonMetric::RotationDeg: S = TEXT("Rotate ") + R; break;
	case ELessonMetric::GrabHoldSeconds: S = TEXT("Hold a grab ") + R; break;
	case ELessonMetric::DiveBeforeTouchdown: S = TEXT("Dive the kite ") + R + TEXT(" before touchdown"); break;
	case ELessonMetric::HeadingChange: S = TEXT("Turn ") + R + TEXT(" in the air"); break;
	case ELessonMetric::Channel: S = TEXT("Bring the ") + SchoolChannelName(O.Channel) + TEXT(" to ") + R; break;
	case ELessonMetric::SpeedHeld: S = TEXT("Hold a speed of ") + R; break;
	case ELessonMetric::KiteElevationHeld: S = TEXT("Fly the kite at ") + R; break;
	case ELessonMetric::ChannelHeld: S = TEXT("Hold the ") + SchoolChannelName(O.Channel) + TEXT(" at ") + R; break;
	case ELessonMetric::TimeToPlaning: S = TEXT("Get planing within ") + SchoolNum(O.Max) + TEXT(" s"); break;
	case ELessonMetric::UpwindGain: S = TEXT("Gain ") + R + TEXT(" upwind"); break;
	case ELessonMetric::DistanceRidden: S = TEXT("Ride ") + R + TEXT(" planing"); break;
	case ELessonMetric::KiteDives:
		S = FString::Printf(TEXT("Dive the kite: never below %s°, back above %s°"), *SchoolNum(O.Min), *SchoolNum(O.Max));
		if (O.WindowSeconds > 0.0f)
		{
			S += TEXT(" within ") + SchoolNum(O.WindowSeconds) + TEXT(" s");
		}
		break;
	case ELessonMetric::Transition: S = TEXT("Turn round"); break;
	case ELessonMetric::TransitionSpeedKept: S = TEXT("Turn round keeping ") + R + TEXT(" of your speed"); break;
	case ELessonMetric::TransitionNotPlaningSeconds: S = TEXT("Turn round, off the plane ") + R; break;
	case ELessonMetric::KiteLeadAtTransition: S = TEXT("Kite leads the turn by ") + R; break;
	default: S = TEXT("No pass test"); break;
	}
	if (SchoolIsHeld(O.Metric) && O.WindowSeconds > 0.0f)
	{
		S += TEXT(" for ") + SchoolNum(O.WindowSeconds) + TEXT(" s");
	}
	if (O.bEachTack && O.Count == 2)
	{
		S += TEXT(", on each tack");
	}
	else if (O.Count > 1)
	{
		S += FString::Printf(TEXT(", %d %s"), O.Count, O.bInARow ? TEXT("in a row") : TEXT("times"));
		if (O.bEachTack)
		{
			S += TEXT(", alternating tacks");
		}
	}
	for (const FLessonCondition& C : O.Conditions)
	{
		const FLessonMeasure& M = C.Measure;
		if (M.Metric == ELessonMetric::LandingGrade)
		{
			S += TEXT(", landed ") + SchoolGradeName(C.Max) + TEXT(" or better");
		}
		else if (M.Metric == ELessonMetric::Popped && C.Min >= 1.0f)
		{
			S += TEXT(", popped with the board");
		}
		else if (M.Metric == ELessonMetric::JumpHeight)
		{
			S += TEXT(", from a jump of ") + SchoolRange(C.Min, C.Max, TEXT(" m"));
		}
		else if (M.Metric == ELessonMetric::DiveBeforeTouchdown)
		{
			S += TEXT(", dive ") + SchoolRange(C.Min, C.Max, TEXT(" s")) + TEXT(" before touchdown");
		}
		else if (M.Metric == ELessonMetric::Channel)
		{
			switch (M.Channel)
			{
			case ELessonChannel::Fallen:
				if (C.Max <= 0.0f)
				{
					S += TEXT(", no fall");
				}
				break;
			case ELessonChannel::KiteElevation:
				if (M.Reduce == ELessonReduce::Range)
				{
					S += TEXT(", kite moving ") + SchoolRange(C.Min, C.Max, TEXT("°")) + TEXT(" at take-off");
				}
				else
				{
					S += TEXT(", kite at ") + SchoolRange(C.Min, C.Max, TEXT("°"));
				}
				break;
			case ELessonChannel::KiteClockAbs:
				S += TEXT(", kite within ") + SchoolNum(C.Max) + TEXT("° of 12");
				break;
			case ELessonChannel::CompletedLoops:
				S += TEXT(", with a kite loop");
				break;
			case ELessonChannel::Toeside:
				S += TEXT(", toeside");
				break;
			case ELessonChannel::Planing:
				if (C.Min >= 1.0f)
				{
					S += TEXT(", planing");
				}
				break;
			default:
				break;
			}
		}
	}
	return FText::FromString(S);
}

FText SchoolMenuText::FormatValue(ELessonMetric Metric, float Value)
{
	using namespace KiteSurfSchoolPrivate;
	if (!FMath::IsFinite(Value))
	{
		return FText::FromString(TEXT("-"));
	}
	switch (Metric)
	{
	case ELessonMetric::LandingGrade:
		return FText::FromString(SchoolGradeName(Value));
	case ELessonMetric::Popped:
	case ELessonMetric::LoopKind:
	case ELessonMetric::TrickNameContains:
		return Value >= 0.5f ? LOCTEXT("Yes", "yes") : LOCTEXT("No", "no");
	default:
		break;
	}
	float Scale = 1.0f;
	const FString Unit = SchoolUnitOf(Metric, Scale);
	const float Shown = Value * Scale;
	// Big numbers in whole units, small ones to a tenth or a hundredth.
	const FString Text = FMath::Abs(Shown) >= 10.0f ? FString::Printf(TEXT("%.0f"), Shown) : SchoolNum(FMath::RoundToFloat(Shown * 10.0f) / 10.0f);
	return FText::FromString(Text + Unit);
}

FText SchoolMenuText::FormatLastPlayed(const FDateTime& PlayedUtc, const FDateTime& NowUtc)
{
	if (PlayedUtc.GetTicks() == 0)
	{
		return LOCTEXT("Never", "never");
	}
	const int32 Days = FMath::FloorToInt((NowUtc.GetDate() - PlayedUtc.GetDate()).GetTotalDays());
	if (Days <= 0)
	{
		return LOCTEXT("Today", "today");
	}
	if (Days == 1)
	{
		return LOCTEXT("Yesterday", "yesterday");
	}
	if (Days < 7)
	{
		return FText::FromString(FString::Printf(TEXT("%d days ago"), Days));
	}
	return FText::FromString(PlayedUtc.ToString(TEXT("%Y-%m-%d")));
}

FText SchoolMenuText::ListAssists(const FLessonAssists& Assists)
{
	using namespace KiteSurfSchoolPrivate;
	TArray<FString> Names;
	for (const int32 Index : SchoolAssistsOn(Assists))
	{
		Names.Add(SchoolAssistName(Index));
	}
	return Names.Num() > 0 ? FText::FromString(FString::Join(Names, TEXT(", "))) : LOCTEXT("NoAssists", "none");
}

FText SchoolMenuText::FeatureName(ELessonFeature Feature)
{
	switch (Feature)
	{
	case ELessonFeature::ToesideRiding: return LOCTEXT("Feature.Toeside", "toeside riding");
	case ELessonFeature::Grabs: return LOCTEXT("Feature.Grabs", "grabs");
	case ELessonFeature::Rotation: return LOCTEXT("Feature.Rotation", "rotations");
	case ELessonFeature::Unhooked: return LOCTEXT("Feature.Unhooked", "unhooked riding");
	case ELessonFeature::Kickers: return LOCTEXT("Feature.Kickers", "kickers");
	default: return FText::GetEmpty();
	}
}

// ---------------------------------------------------------------------------------------------
// UKiteSurfSchoolWidget: state
// ---------------------------------------------------------------------------------------------

UKiteSurfSchoolWidget::UKiteSurfSchoolWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

void UKiteSurfSchoolWidget::SetLessonSubsystem(ULessonSubsystem* InLessons)
{
	LessonsOverride = InLessons;
	FocusedLessonId = NAME_None;
	Refresh();
	if (Navigator.Num() > 0)
	{
		BuildNavigation();
		SelectItem(Navigator.DefaultIndex);
	}
}

ULessonSubsystem* UKiteSurfSchoolWidget::GetLessons() const
{
	if (LessonsOverride)
	{
		return LessonsOverride.Get();
	}
	UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<ULessonSubsystem>() : nullptr;
}

void UKiteSurfSchoolWidget::Refresh()
{
	const ULessonSubsystem* Lessons = GetLessons();
	Tiles = Lessons ? Lessons->GetLessonList() : KiteSurfSchoolPrivate::SchoolFreshList();
	bRefreshed = true;
	if (!FindTile(FocusedLessonId))
	{
		FocusLesson(GetContinueLessonId());
	}
}

const FLessonListItem* UKiteSurfSchoolWidget::FindTile(FName LessonId) const
{
	return Tiles.FindByPredicate([LessonId](const FLessonListItem& Item) { return Item.LessonId == LessonId; });
}

FText UKiteSurfSchoolWidget::GetTileBadge(FName LessonId) const
{
	const FLessonListItem* Tile = FindTile(LessonId);
	if (!Tile)
	{
		return FText::GetEmpty();
	}
	if (!Tile->bAvailable)
	{
		return LOCTEXT("Badge.Soon", "COMING SOON");
	}
	if (Tile->bLocked)
	{
		return LOCTEXT("Badge.Locked", "LOCKED");
	}
	if (Tile->bNew)
	{
		return LOCTEXT("Badge.New", "NEW");
	}
	return FText::GetEmpty();
}

const FLessonDef* UKiteSurfSchoolWidget::GetFocusedLesson() const
{
	return LessonCatalog::Find(FocusedLessonId);
}

TArray<FName> UKiteSurfSchoolWidget::ChapterLessons(FName Chapter) const
{
	TArray<FName> Ids;
	for (const FLessonDef& Lesson : LessonCatalog::GetAll())
	{
		if (Lesson.Chapter == Chapter)
		{
			Ids.Add(Lesson.Id);
		}
	}
	return Ids;
}

void UKiteSurfSchoolWidget::FocusLesson(FName LessonId)
{
	const FLessonDef* Lesson = LessonCatalog::Find(LessonId);
	if (!Lesson)
	{
		return;
	}
	if (FocusedLessonId != LessonId)
	{
		RunWindKnots = Lesson->Setup.WindKnots;
		AssistChoice = 0;
	}
	FocusedLessonId = LessonId;
	Column = FMath::Max(ChapterLessons(Lesson->Chapter).IndexOfByKey(LessonId), 0);
}

FName UKiteSurfSchoolWidget::GetContinueLessonId() const
{
	const ULessonSubsystem* Lessons = GetLessons();
	return Lessons ? Lessons->GetRecommendedNext() : LessonUnlock::RecommendedNext(FLessonProgressBook());
}

bool UKiteSurfSchoolWidget::CanStartFocused() const
{
	const ULessonSubsystem* Lessons = GetLessons();
	return Lessons && Lessons->IsUnlocked(FocusedLessonId);
}

bool UKiteSurfSchoolWidget::StartFocusedLesson()
{
	return StartLessonWith(FocusedLessonId, GetRunOptions());
}

bool UKiteSurfSchoolWidget::Continue()
{
	const FName Id = GetContinueLessonId();
	FocusLesson(Id);
	return StartLessonWith(Id, FLessonRunOptions());
}

bool UKiteSurfSchoolWidget::StartLessonWith(FName LessonId, const FLessonRunOptions& Options)
{
	ULessonSubsystem* Lessons = GetLessons();
	if (!Lessons || !Lessons->StartLessonWithOptions(LessonId, Options))
	{
		KiteSurfMenuStyle::PlayMenuSound(this, EKiteMenuSound::Back);
		return false;
	}
	OnLessonStartedDelegate.Broadcast(LessonId);
	RemoveFromParent();
	return true;
}

void UKiteSurfSchoolWidget::SetRunWindKnots(float Knots)
{
	const FLessonDef* Lesson = GetFocusedLesson();
	if (!Lesson)
	{
		return;
	}
	const float Low = Lesson->Setup.WindKnots;
	const float High = FMath::Min(Low + MaxExtraWindKnots, KiteGear::MaxWindKnots);
	RunWindKnots = FMath::Clamp(FMath::RoundToFloat(Knots), Low, High);
}

int32 UKiteSurfSchoolWidget::GetAssistChoiceCount() const
{
	const FLessonDef* Lesson = GetFocusedLesson();
	const int32 On = Lesson ? KiteSurfSchoolPrivate::SchoolAssistsOn(Lesson->Setup.Assists).Num() : 0;
	// The lesson's own; then each one left off and all off (one assist: just "all off").
	return On == 0 ? 1 : (On == 1 ? 2 : On + 2);
}

void UKiteSurfSchoolWidget::StepAssistChoice(int32 Direction)
{
	const int32 Count = GetAssistChoiceCount();
	AssistChoice = ((AssistChoice + (Direction < 0 ? -1 : 1)) % Count + Count) % Count;
}

FText UKiteSurfSchoolWidget::GetAssistChoiceText() const
{
	using namespace KiteSurfSchoolPrivate;
	const FLessonDef* Lesson = GetFocusedLesson();
	if (!Lesson)
	{
		return FText::GetEmpty();
	}
	const TArray<int32> On = SchoolAssistsOn(Lesson->Setup.Assists);
	if (AssistChoice == 0)
	{
		return FText::FromString(TEXT("LESSON'S: ") + SchoolMenuText::ListAssists(Lesson->Setup.Assists).ToString());
	}
	if (On.Num() >= 2 && AssistChoice <= On.Num())
	{
		return FText::FromString(FString(TEXT("WITHOUT ")) + SchoolAssistName(On[AssistChoice - 1]));
	}
	return LOCTEXT("AssistsAllOff", "ALL OFF");
}

FLessonRunOptions UKiteSurfSchoolWidget::GetRunOptions() const
{
	using namespace KiteSurfSchoolPrivate;
	FLessonRunOptions Options;
	const FLessonDef* Lesson = GetFocusedLesson();
	if (!Lesson)
	{
		return Options;
	}
	if (RunWindKnots > Lesson->Setup.WindKnots + 0.01f)
	{
		Options.WindKnots = RunWindKnots;
	}
	if (AssistChoice > 0)
	{
		const TArray<int32> On = SchoolAssistsOn(Lesson->Setup.Assists);
		Options.bOverrideAssists = true;
		Options.Assists = Lesson->Setup.Assists;
		if (On.Num() >= 2 && AssistChoice <= On.Num())
		{
			SchoolAssistFlag(Options.Assists, On[AssistChoice - 1]) = false;
		}
		else
		{
			Options.Assists = FLessonAssists();
		}
	}
	return Options;
}

void UKiteSurfSchoolWidget::RequestReset()
{
	bConfirmingReset = true;
	BuildNavigation();
	SelectItem(CancelResetItem);
}

void UKiteSurfSchoolWidget::ConfirmReset()
{
	if (ULessonSubsystem* Lessons = GetLessons())
	{
		Lessons->ResetProgress();
	}
	bConfirmingReset = false;
	FocusedLessonId = NAME_None;
	Refresh();
	BuildNavigation();
	SelectItem(ContinueItem);
}

void UKiteSurfSchoolWidget::CancelReset()
{
	bConfirmingReset = false;
	BuildNavigation();
	SelectItem(ResetItem);
}

void UKiteSurfSchoolWidget::Close()
{
	OnClosedDelegate.Broadcast();
	RemoveFromParent();
}

// ---------------------------------------------------------------------------------------------
// What the panels show
// ---------------------------------------------------------------------------------------------

FText UKiteSurfSchoolWidget::GetOverallProgressText() const
{
	const ULessonSubsystem* Lessons = GetLessons();
	const int32 Stars = Lessons ? Lessons->GetTotalStars() : 0;
	const int32 MaxStars = LessonCatalog::GetAll().Num() * FLessonProgressBook::MaxStars;
	const UKiteSurfGameInstance* GI = Cast<UKiteSurfGameInstance>(Lessons ? Lessons->GetGameInstance() : GetGameInstance());
	const int32 Tricks = GI ? GI->GetTrickBook().Num() : 0;
	return FText::FromString(FString::Printf(TEXT("STARS  %d / %d        TRICK BOOK  %d %s"), Stars, MaxStars, Tricks, Tricks == 1 ? TEXT("trick") : TEXT("tricks")));
}

FText UKiteSurfSchoolWidget::GetChapterProgressText(FName Chapter) const
{
	if (ChapterLessons(Chapter).Num() == 0)
	{
		return FText::GetEmpty();
	}
	const ULessonSubsystem* Lessons = GetLessons();
	const float Completion = Lessons ? Lessons->GetChapterCompletion(Chapter) : 0.0f;
	return FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Completion * 100.0f)));
}

FText UKiteSurfSchoolWidget::GetDetailStatusText() const
{
	const FLessonListItem* Tile = FindTile(FocusedLessonId);
	const FLessonDef* Lesson = GetFocusedLesson();
	if (!Tile || !Lesson)
	{
		return FText::GetEmpty();
	}
	if (!Tile->bAvailable)
	{
		return FText::FromString(FString::Printf(TEXT("COMING SOON: the game has no %s yet"), *SchoolMenuText::FeatureName(Lesson->RequiredFeature).ToString()));
	}
	if (Tile->bLocked)
	{
		const ULessonSubsystem* Lessons = GetLessons();
		TArray<FString> Missing;
		for (const FName& Req : Lesson->Requires)
		{
			if (!Lessons || Lessons->GetProgress().GetStars(Req) == 0)
			{
				Missing.Add(KiteSurfSchoolPrivate::SchoolLessonLabel(Req));
			}
		}
		return FText::FromString(FString::Printf(TEXT("LOCKED: pass %s first"), *FString::Join(Missing, TEXT(" and "))));
	}
	if (Tile->Stars > 0)
	{
		return FText::FromString(FString::Printf(TEXT("PASSED: %d %s"), Tile->Stars, Tile->Stars == 1 ? TEXT("star") : TEXT("stars")));
	}
	return Tile->bNew ? LOCTEXT("Status.New", "NEW") : LOCTEXT("Status.NotPassed", "NOT PASSED YET");
}

FText UKiteSurfSchoolWidget::GetDetailTeachText() const
{
	const FLessonDef* Lesson = GetFocusedLesson();
	if (!Lesson)
	{
		return FText::GetEmpty();
	}
	FString S = Lesson->Summary.ToString();
	for (int32 I = 0; I < Lesson->Steps.Num(); ++I)
	{
		S += FString::Printf(TEXT("\n%d. %s"), I + 1, *Lesson->Steps[I].Prompt.ToString());
	}
	S += FString::Printf(TEXT("\n%.0f kn, assists: %s"), Lesson->Setup.WindKnots, *SchoolMenuText::ListAssists(Lesson->Setup.Assists).ToString());
	return FText::FromString(S);
}

FText UKiteSurfSchoolWidget::GetDetailNeedsText() const
{
	const FLessonDef* Lesson = GetFocusedLesson();
	if (!Lesson)
	{
		return FText::GetEmpty();
	}
	const ULessonSubsystem* Lessons = GetLessons();
	TArray<FString> Parts;
	for (const FName& Req : Lesson->Requires)
	{
		const bool bPassed = Lessons && Lessons->GetProgress().GetStars(Req) > 0;
		Parts.Add(KiteSurfSchoolPrivate::SchoolLessonLabel(Req) + (bPassed ? TEXT(" (passed)") : TEXT(" (not passed)")));
	}
	FString S = Parts.Num() > 0 ? FString::Join(Parts, TEXT(", ")) : FString(TEXT("Nothing: start here."));
	if (!LessonCatalog::IsAvailable(*Lesson))
	{
		S += FString::Printf(TEXT("\n%s (coming soon)"), *SchoolMenuText::FeatureName(Lesson->RequiredFeature).ToString());
	}
	return FText::FromString(S);
}

FText UKiteSurfSchoolWidget::GetDetailPassText() const
{
	const FLessonDef* Lesson = GetFocusedLesson();
	if (!Lesson)
	{
		return FText::GetEmpty();
	}
	FString S = SchoolMenuText::DescribeObjective(Lesson->Pass).ToString() + TEXT(".");
	const FString Higher = Lesson->Stars.HigherBarText.ToString();
	if (Lesson->Setup.Assists.CountOn() > 0)
	{
		S += Higher.IsEmpty() ? FString(TEXT("\n2 stars: fewer assists.")) : FString::Printf(TEXT("\n2 stars: fewer assists, or %s."), *Higher);
		S += TEXT(" 3 stars: every assist off.");
	}
	else if (!Higher.IsEmpty())
	{
		S += FString::Printf(TEXT("\n3 stars: %s."), *Higher);
	}
	return FText::FromString(S);
}

FText UKiteSurfSchoolWidget::GetDetailBestText() const
{
	const ULessonSubsystem* Lessons = GetLessons();
	const FLessonRecord* Record = Lessons ? Lessons->GetProgress().Find(FocusedLessonId) : nullptr;
	if (!Record || Record->Attempts == 0)
	{
		return LOCTEXT("Best.None", "Not played yet.");
	}
	FString S = FString::Printf(TEXT("Best stars %d / %d"), Record->BestStars, FLessonProgressBook::MaxStars);
	if (Record->bHasBestValue)
	{
		S += TEXT("     Best ") + SchoolMenuText::FormatValue(Record->BestValueMetric, Record->BestValue).ToString();
	}
	S += FString::Printf(TEXT("\nAttempts %d     Passes %d     Last played %s"), Record->Attempts, Record->Passes,
		*SchoolMenuText::FormatLastPlayed(Record->LastPlayedUtc, FDateTime::UtcNow()).ToString());
	return FText::FromString(S);
}

bool UKiteSurfSchoolWidget::IsWatchDemoVisible() const
{
	const FLessonDef* Lesson = GetFocusedLesson();
	return Lesson && !Lesson->DemoId.IsNone();
}

// ---------------------------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------------------------

int32 UKiteSurfSchoolWidget::GetChapterItem(FName Chapter) const
{
	const int32* Item = ChapterItems.Find(Chapter);
	return Item ? *Item : INDEX_NONE;
}

void UKiteSurfSchoolWidget::SelectItem(int32 Index)
{
	if (Index != INDEX_NONE)
	{
		Navigator.Select(Index);
	}
}

void UKiteSurfSchoolWidget::FocusFirst()
{
	if (!bRefreshed)
	{
		Refresh();
	}
	BuildNavigation();
	SelectItem(Navigator.DefaultIndex);
	if (const TSharedPtr<SWidget> Widget = GetCachedWidget())
	{
		FSlateApplication::Get().SetKeyboardFocus(Widget);
	}
}

FKiteMenuNavigator& UKiteSurfSchoolWidget::GetNavigator()
{
	if (Navigator.Num() == 0)
	{
		if (!bRefreshed)
		{
			Refresh();
		}
		BuildNavigation();
		SelectItem(Navigator.DefaultIndex);
	}
	return Navigator;
}

FKiteMenuNavigator::FItem UKiteSurfSchoolWidget::MakeChapterItem(FName Chapter)
{
	FKiteMenuNavigator::FItem Item;
	// Left and right walk along the chapter's tiles; accept starts the one in focus.
	Item.Adjust = [this, Chapter](int32 Direction)
	{
		const TArray<FName> Ids = ChapterLessons(Chapter);
		if (Ids.Num() == 0)
		{
			return;
		}
		const int32 Here = Ids.IndexOfByKey(FocusedLessonId);
		const int32 Next = Here == INDEX_NONE ? 0 : FMath::Clamp(Here + Direction, 0, Ids.Num() - 1);
		FocusLesson(Ids[Next]);
	};
	Item.Activate = [this]() { StartFocusedLesson(); };
	Item.Highlight = [this, Chapter](bool bSelected)
	{
		if (!bSelected)
		{
			if (HighlightedChapter == Chapter)
			{
				HighlightedChapter = NAME_None;
			}
			return;
		}
		HighlightedChapter = Chapter;
		// Coming into the row: focus the tile in the column the selection came from.
		const FLessonDef* Focused = GetFocusedLesson();
		const TArray<FName> Ids = ChapterLessons(Chapter);
		if (Ids.Num() > 0 && (!Focused || Focused->Chapter != Chapter))
		{
			const int32 Carried = Column;
			FocusLesson(Ids[FMath::Clamp(Carried, 0, Ids.Num() - 1)]);
			Column = Carried;
		}
	};
	return Item;
}

void UKiteSurfSchoolWidget::BuildNavigation()
{
	Navigator.Reset();
	ChapterItems.Reset();
	HighlightedChapter = NAME_None;
	ContinueItem = WindItem = AssistsItem = StartItem = DemoItem = ResetItem = BackItem = ConfirmResetItem = CancelResetItem = INDEX_NONE;
	Navigator.OnAction = [this](FKiteMenuNavigator::EAction Action)
	{
		KiteSurfMenuStyle::PlayMenuSound(this, Action == FKiteMenuNavigator::EAction::Activated ? EKiteMenuSound::Select : EKiteMenuSound::Move);
	};

	if (bConfirmingReset)
	{
		ConfirmResetItem = Navigator.AddButton(ConfirmResetButton, [this]() { ConfirmReset(); });
		CancelResetItem = Navigator.AddButton(CancelResetButton, [this]() { CancelReset(); });
		Navigator.DefaultIndex = CancelResetItem;
		return;
	}

	auto ButtonHighlight = [](const TSharedPtr<SButton>& Button)
	{
		const TWeakPtr<SButton> Weak = Button;
		return [Weak](bool bSelected)
		{
			if (const TSharedPtr<SButton> Pinned = Weak.Pin())
			{
				Pinned->SetBorderBackgroundColor(bSelected ? KiteSurfSchoolPrivate::SchoolSelectedColor : FLinearColor::White);
			}
		};
	};

	ContinueItem = Navigator.AddButton(ContinueButton, [this]() { Continue(); });
	for (const FSchoolChapterInfo& Chapter : SchoolMenuText::GetChapters())
	{
		if (ChapterLessons(Chapter.Id).Num() > 0)
		{
			ChapterItems.Add(Chapter.Id, Navigator.AddItem(MakeChapterItem(Chapter.Id)));
		}
	}

	FKiteMenuNavigator::FItem Wind;
	Wind.Adjust = [this](int32 Direction) { SetRunWindKnots(RunWindKnots + Direction); };
	Wind.Activate = [this]()
	{
		// Accept steps the wind up, and from the top back to the lesson's own.
		const float Before = RunWindKnots;
		SetRunWindKnots(RunWindKnots + 1.0f);
		if (RunWindKnots == Before)
		{
			SetRunWindKnots(0.0f);
		}
	};
	Wind.Highlight = ButtonHighlight(WindButton);
	WindItem = Navigator.AddItem(MoveTemp(Wind));

	FKiteMenuNavigator::FItem Assists;
	Assists.Adjust = [this](int32 Direction) { StepAssistChoice(Direction); };
	Assists.Activate = [this]() { StepAssistChoice(1); };
	Assists.Highlight = ButtonHighlight(AssistsButton);
	AssistsItem = Navigator.AddItem(MoveTemp(Assists));

	StartItem = Navigator.AddButton(StartButton, [this]() { StartFocusedLesson(); });
	if (IsWatchDemoVisible())
	{
		DemoItem = Navigator.AddButton(DemoButton, []() {});
	}
	ResetItem = Navigator.AddButton(ResetButton, [this]() { RequestReset(); });
	BackItem = Navigator.AddButton(BackButton, [this]() { Close(); });
	Navigator.DefaultIndex = ContinueItem;
}

void UKiteSurfSchoolWidget::NativeConstruct()
{
	Super::NativeConstruct();
	FocusFirst();
}

FReply UKiteSurfSchoolWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (GetNavigator().HandleKey(Key))
	{
		return FReply::Handled();
	}
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Right)
	{
		KiteSurfMenuStyle::PlayMenuSound(this, EKiteMenuSound::Back);
		if (bConfirmingReset)
		{
			CancelReset();
		}
		else
		{
			Close();
		}
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// ---------------------------------------------------------------------------------------------
// Slate
// ---------------------------------------------------------------------------------------------

TSharedRef<SWidget> UKiteSurfSchoolWidget::MakeButton(TSharedPtr<SButton>& OutButton, TAttribute<FText> Label, TFunction<void()> OnClicked, int32 FontSize)
{
	return SAssignNew(OutButton, SButton)
		.IsFocusable(false)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.OnClicked_Lambda([OnClicked]()
		{
			OnClicked();
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", FontSize))
			.Margin(FMargin(10.0f, 5.0f))
		];
}

TSharedRef<SWidget> UKiteSurfSchoolWidget::BuildTile(FName LessonId, FName Chapter)
{
	using namespace KiteSurfSchoolPrivate;
	const FLessonDef* Lesson = LessonCatalog::Find(LessonId);
	const FText Title = Lesson ? Lesson->Title : FText::GetEmpty();

	// No stars on a locked or coming-soon tile: its badge takes the space.
	TSharedRef<SHorizontalBox> Stars = SNew(SHorizontalBox)
		.Visibility_Lambda([this, LessonId]()
		{
			const FLessonListItem* Tile = FindTile(LessonId);
			return Tile && Tile->bAvailable && (!Tile->bLocked || Tile->Stars > 0) ? EVisibility::Visible : EVisibility::Collapsed;
		});
	for (int32 Star = 1; Star <= FLessonProgressBook::MaxStars; ++Star)
	{
		Stars->AddSlot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(13.0f)
				.HeightOverride(13.0f)
				[
					SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor_Lambda([this, LessonId, Star]()
					{
						const FLessonListItem* Tile = FindTile(LessonId);
						return FSlateColor(Tile && Tile->Stars >= Star ? SchoolStarOnColor : SchoolStarOffColor);
					})
				]
			];
	}

	auto TextColor = [this, LessonId]()
	{
		const FLessonListItem* Tile = FindTile(LessonId);
		return FSlateColor(Tile && !Tile->bLocked ? FLinearColor::White : SchoolDimColor);
	};

	return SNew(SBox)
		.WidthOverride(104.0f)
		.HeightOverride(86.0f)
		[
			// The outline: gold on the tile in focus while its row is selected.
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.Padding(3.0f)
			.BorderBackgroundColor_Lambda([this, LessonId, Chapter]()
			{
				if (FocusedLessonId != LessonId)
				{
					return FSlateColor(FLinearColor::Transparent);
				}
				return FSlateColor(HighlightedChapter == Chapter ? SchoolSelectedColor : FLinearColor(0.8f, 0.85f, 0.9f, 0.55f));
			})
			.OnMouseButtonDown_Lambda([this, LessonId, Chapter](const FGeometry&, const FPointerEvent& Event)
			{
				if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
				{
					return FReply::Unhandled();
				}
				FocusLesson(LessonId);
				SelectItem(GetChapterItem(Chapter));
				KiteSurfMenuStyle::PlayMenuSound(this, EKiteMenuSound::Move);
				return FReply::Handled();
			})
			.OnMouseDoubleClick_Lambda([this, LessonId](const FGeometry&, const FPointerEvent&)
			{
				FocusLesson(LessonId);
				StartFocusedLesson();
				return FReply::Handled();
			})
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.Padding(FMargin(7.0f, 5.0f))
				.BorderBackgroundColor_Lambda([this, LessonId]()
				{
					const FLessonListItem* Tile = FindTile(LessonId);
					if (!Tile || !Tile->bAvailable)
					{
						return FSlateColor(FLinearColor(0.07f, 0.08f, 0.09f, 0.92f));
					}
					if (Tile->bLocked)
					{
						return FSlateColor(FLinearColor(0.05f, 0.08f, 0.13f, 0.92f));
					}
					return FSlateColor(Tile->Stars > 0 ? FLinearColor(0.04f, 0.26f, 0.3f, 0.95f) : FLinearColor(0.05f, 0.18f, 0.38f, 0.95f));
				})
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromName(LessonId))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
						.ColorAndOpacity_Lambda(TextColor)
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					.Padding(0.0f, 1.0f)
					[
						SNew(STextBlock)
						.Text(Title)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.ColorAndOpacity_Lambda(TextColor)
						.AutoWrapText(true)
					]
					// Stars on the left, the badge (NEW, LOCKED, COMING SOON) on the right.
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							Stars
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text_Lambda([this, LessonId]() { return GetTileBadge(LessonId); })
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
							.ColorAndOpacity_Lambda([this, LessonId]()
							{
								const FLessonListItem* Tile = FindTile(LessonId);
								return FSlateColor(Tile && Tile->bNew ? SchoolNewColor : SchoolDimColor);
							})
						]
					]
				]
			]
		];
}

TSharedRef<SWidget> UKiteSurfSchoolWidget::BuildChapter(const FSchoolChapterInfo& Chapter)
{
	using namespace KiteSurfSchoolPrivate;
	const TArray<FName> Ids = ChapterLessons(Chapter.Id);
	const FName ChapterId = Chapter.Id;

	TSharedRef<SWidget> Content = SNullWidget::NullWidget;
	if (Ids.Num() == 0)
	{
		Content = SNew(STextBlock)
			.Text(LOCTEXT("ChapterSoon", "Lessons coming soon"))
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 10))
			.ColorAndOpacity(SchoolDimColor);
	}
	else
	{
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		for (const FName& Id : Ids)
		{
			Row->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)[BuildTile(Id, ChapterId)];
		}
		Content = Row;
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Bottom)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("%s  %s"), *Chapter.Id.ToString(), *Chapter.Name.ToString())))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
				.ColorAndOpacity_Lambda([this, ChapterId, bHasLessons = Ids.Num() > 0]()
				{
					if (!bHasLessons)
					{
						return FSlateColor(SchoolDimColor);
					}
					return FSlateColor(HighlightedChapter == ChapterId ? SchoolSelectedColor : SchoolLabelColor);
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Bottom)
			.Padding(12.0f, 0.0f, 0.0f, 1.0f)
			[
				SNew(STextBlock)
				.Text(Chapter.Wind)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				.ColorAndOpacity(SchoolHintColor)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Bottom)
			[
				SNew(STextBlock)
				.Text_Lambda([this, ChapterId]() { return GetChapterProgressText(ChapterId); })
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
				.ColorAndOpacity(SchoolHintColor)
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			Content
		];
}

TSharedRef<SWidget> UKiteSurfSchoolWidget::BuildMapPanel()
{
	using namespace KiteSurfSchoolPrivate;
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(20.0f, 14.0f, 20.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SchoolTitle", "SCHOOL"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 26))
			.ColorAndOpacity(SchoolTitleColor)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(20.0f, 0.0f, 20.0f, 10.0f)
		[
			SNew(STextBlock)
			.Text_Lambda([this]() { return GetOverallProgressText(); })
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
			.ColorAndOpacity(SchoolHintColor)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(20.0f, 0.0f, 20.0f, 12.0f)
		[
			MakeButton(ContinueButton, TAttribute<FText>::CreateLambda([this]()
			{
				const FName Id = GetContinueLessonId();
				return Id.IsNone() ? LOCTEXT("ContinueNone", "CONTINUE")
					: FText::FromString(TEXT("CONTINUE:  ") + KiteSurfSchoolPrivate::SchoolLessonLabel(Id).ToUpper());
			}), [this]() { Continue(); }, 17)
		];

	for (const FSchoolChapterInfo& Chapter : SchoolMenuText::GetChapters())
	{
		Box->AddSlot()
			.AutoHeight()
			.Padding(20.0f, 4.0f, 20.0f, 6.0f)
			[
				BuildChapter(Chapter)
			];
	}

	Box->AddSlot()
		.AutoHeight()
		.Padding(20.0f, 10.0f, 20.0f, 14.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				MakeButton(ResetButton, LOCTEXT("ResetProgress", "RESET PROGRESS"), [this]() { RequestReset(); }, 13)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				MakeButton(BackButton, LOCTEXT("Back", "BACK"), [this]() { Close(); }, 16)
			]
		];
	return Box;
}

TSharedRef<SWidget> UKiteSurfSchoolWidget::BuildDetailPanel()
{
	using namespace KiteSurfSchoolPrivate;
	auto Section = [](const FText& Label) -> TSharedRef<SWidget>
	{
		return SNew(STextBlock)
			.Text(Label)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			.ColorAndOpacity(SchoolHintColor);
	};
	auto Body = [](TFunction<FText()> Text) -> TSharedRef<SWidget>
	{
		return SNew(STextBlock)
			.Text_Lambda([Text]() { return Text(); })
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
			.ColorAndOpacity(FLinearColor(0.92f, 0.95f, 1.0f))
			.AutoWrapText(true);
	};
	auto ChoiceRow = [this](const FText& Label, TSharedPtr<SButton>& OutButton, TAttribute<FText> Value, TFunction<void()> OnClicked) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(90.0f)
				[
					SNew(STextBlock)
					.Text(Label)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
					.ColorAndOpacity(SchoolLabelColor)
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				MakeButton(OutButton, Value, OnClicked, 12)
			];
	};
	constexpr float Pad = 18.0f;

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(Pad, 16.0f, Pad, 2.0f)
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				const FLessonDef* Lesson = GetFocusedLesson();
				return Lesson ? FText::FromString(FString::Printf(TEXT("%s  %s"), *Lesson->Id.ToString(), *Lesson->Title.ToString().ToUpper())) : FText::GetEmpty();
			})
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 20))
			.ColorAndOpacity(SchoolTitleColor)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(Pad, 0.0f, Pad, 10.0f)
		[
			SNew(STextBlock)
			.Text_Lambda([this]() { return GetDetailStatusText(); })
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
			.AutoWrapText(true)
			.ColorAndOpacity_Lambda([this]()
			{
				const FLessonListItem* Tile = FindTile(FocusedLessonId);
				if (!Tile || Tile->bLocked)
				{
					return FSlateColor(SchoolDimColor);
				}
				return FSlateColor(Tile->bNew ? SchoolNewColor : SchoolStarOnColor);
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Pad, 4.0f, Pad, 1.0f)[Section(LOCTEXT("Teaches", "WHAT IT TEACHES"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(Pad, 0.0f, Pad, 6.0f)[Body([this]() { return GetDetailTeachText(); })]
		+ SVerticalBox::Slot().AutoHeight().Padding(Pad, 4.0f, Pad, 1.0f)[Section(LOCTEXT("Needs", "NEEDS"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(Pad, 0.0f, Pad, 6.0f)[Body([this]() { return GetDetailNeedsText(); })]
		+ SVerticalBox::Slot().AutoHeight().Padding(Pad, 4.0f, Pad, 1.0f)[Section(LOCTEXT("ToPass", "TO PASS"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(Pad, 0.0f, Pad, 6.0f)[Body([this]() { return GetDetailPassText(); })]
		+ SVerticalBox::Slot().AutoHeight().Padding(Pad, 4.0f, Pad, 1.0f)[Section(LOCTEXT("YourBest", "YOUR BEST"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(Pad, 0.0f, Pad, 10.0f)[Body([this]() { return GetDetailBestText(); })]
		+ SVerticalBox::Slot().AutoHeight().Padding(Pad, 4.0f, Pad, 4.0f)
		[
			ChoiceRow(LOCTEXT("Wind", "WIND"), WindButton, TAttribute<FText>::CreateLambda([this]()
			{
				const FLessonDef* Lesson = GetFocusedLesson();
				const float Extra = Lesson ? RunWindKnots - Lesson->Setup.WindKnots : 0.0f;
				return FText::FromString(Extra > 0.5f ? FString::Printf(TEXT("%.0f kn  (+%.0f)"), RunWindKnots, Extra)
					: FString::Printf(TEXT("%.0f kn  (LESSON'S)"), RunWindKnots));
			}), [this]()
			{
				const float Before = RunWindKnots;
				SetRunWindKnots(RunWindKnots + 1.0f);
				if (RunWindKnots == Before)
				{
					SetRunWindKnots(0.0f);
				}
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Pad, 4.0f, Pad, 2.0f)
		[
			ChoiceRow(LOCTEXT("Assists", "ASSISTS"), AssistsButton, TAttribute<FText>::CreateLambda([this]() { return GetAssistChoiceText(); }),
				[this]() { StepAssistChoice(1); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Pad + 90.0f, 0.0f, Pad, 8.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AssistHint", "Fewer assists earn 2 stars, none at all 3."))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(SchoolHintColor)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Pad, 4.0f, Pad, 4.0f)
		[
			MakeButton(StartButton, TAttribute<FText>::CreateLambda([this]()
			{
				const FLessonListItem* Tile = FindTile(FocusedLessonId);
				if (Tile && !Tile->bAvailable)
				{
					return LOCTEXT("StartSoon", "COMING SOON");
				}
				if (!CanStartFocused())
				{
					return LOCTEXT("StartLocked", "LOCKED");
				}
				const ULessonSubsystem* Lessons = GetLessons();
				const FLessonRecord* Record = Lessons ? Lessons->GetProgress().Find(FocusedLessonId) : nullptr;
				return Record && Record->Attempts > 0 ? LOCTEXT("StartAgain", "START AGAIN") : LOCTEXT("Start", "START");
			}), [this]() { StartFocusedLesson(); }, 18)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Pad, 0.0f, Pad, 16.0f)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return IsWatchDemoVisible() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				MakeButton(DemoButton, LOCTEXT("WatchDemo", "WATCH DEMO"), []() {}, 14)
			]
		];
}

TSharedRef<SWidget> UKiteSurfSchoolWidget::BuildConfirmPanel()
{
	using namespace KiteSurfSchoolPrivate;
	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.0f, 0.01f, 0.03f, 0.7f))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.Visibility_Lambda([this]() { return bConfirmingReset ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SBox)
			.WidthOverride(480.0f)
			[
				KiteSurfMenuStyle::BuildPanel(
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(20.0f, 18.0f, 20.0f, 8.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ResetTitle", "RESET PROGRESS?"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 22))
						.ColorAndOpacity(SchoolTitleColor)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(20.0f, 0.0f, 20.0f, 14.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ResetBody", "Every lesson's stars, bests and attempts are forgotten. Your trick book is kept."))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
						.ColorAndOpacity(SchoolLabelColor)
						.AutoWrapText(true)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(20.0f, 0.0f, 20.0f, 18.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							MakeButton(ConfirmResetButton, LOCTEXT("ResetConfirm", "RESET"), [this]() { ConfirmReset(); }, 16)
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							MakeButton(CancelResetButton, LOCTEXT("ResetCancel", "CANCEL"), [this]() { CancelReset(); }, 16)
						]
					])
			]
		];
}

TSharedRef<SWidget> UKiteSurfSchoolWidget::RebuildWidget()
{
	if (WidgetTree && WidgetTree->RootWidget)
	{
		return Super::RebuildWidget();
	}
	if (!bRefreshed)
	{
		Refresh();
	}
	BackgroundTexture = KiteSurfMenuStyle::LoadBackgroundTexture();
	KiteSurfMenuStyle::SetupBackgroundBrush(BackgroundBrush, BackgroundTexture);

	const TSharedRef<SWidget> Panels = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Top)
		[
			SNew(SBox)
			.WidthOverride(800.0f)
			[
				KiteSurfMenuStyle::BuildPanel(BuildMapPanel())
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Top)
		.Padding(18.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(470.0f)
			[
				KiteSurfMenuStyle::BuildPanel(BuildDetailPanel())
			]
		];

	TSharedRef<SWidget> Root = SNullWidget::NullWidget;
	if (bDuringRide)
	{
		// Over the paused game: dim it rather than replace it with the menu art.
		Root = SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.0f, 0.02f, 0.05f, 0.75f))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				Panels
			];
	}
	else
	{
		Root = KiteSurfMenuStyle::BuildBackdrop(&BackgroundBrush, BackgroundTexture != nullptr, KiteSurfMenuStyle::MenuLoopBrush(GetGameInstance()),
			SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				Panels
			]);
	}

	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			Root
		]
		+ SOverlay::Slot()
		[
			BuildConfirmPanel()
		];
}

#undef LOCTEXT_NAMESPACE
