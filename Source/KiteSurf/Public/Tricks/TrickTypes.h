#pragma once

#include "CoreMinimal.h"
#include "TrickTypes.generated.h"

/**
 * The enums every trick module shares (docs/tricks.md section 2 for the words).
 *
 * Rider frame: Up is feet to head, Front is where the chest faces, Side is hip to hip towards
 * the nose. Back and front, frontside and backside are defined in docs/tricks.md section 2 from
 * coaching text and are not yet checked against footage; the naming tests pin these enum
 * meanings, not the physics.
 */

/** How a landing went, in WOO's words. Indexes the execution factors in FTrickScoringSettings. */
UENUM(BlueprintType)
enum class ELandingGrade : uint8
{
	Stomped UMETA(DisplayName = "Stomped"),
	Clean   UMETA(DisplayName = "Clean"),
	Sketchy UMETA(DisplayName = "Sketchy"),
	Crash   UMETA(DisplayName = "Crash")
};

/** Why a landing was graded down, for the one-line failure message (backlog F6). */
UENUM(BlueprintType)
enum class ELandingCause : uint8
{
	None            UMETA(DisplayName = "None"),
	OverRotated     UMETA(DisplayName = "Over-rotated"),
	UnderRotated    UMETA(DisplayName = "Under-rotated"),
	/** The board met the water too far across its direction of travel. */
	Sideways        UMETA(DisplayName = "Sideways"),
	/** The rider's up pointed below the horizon at contact. */
	Inverted        UMETA(DisplayName = "Inverted"),
	/** The kite was under the hot-landing elevation: the rider is dragged off the landing. */
	KiteTooLow      UMETA(DisplayName = "Kite too low"),
	TooHard         UMETA(DisplayName = "Too hard"),
	/** The board was off the feet at contact (not caught). */
	BoardOff        UMETA(DisplayName = "Board not caught"),
	BarLost         UMETA(DisplayName = "Bar lost"),
	PassUnfinished  UMETA(DisplayName = "Pass not finished"),
	/** Strapless (T4): the board was under the feet but not lined up with them. */
	BoardNotAligned UMETA(DisplayName = "Board not aligned")
};

/** Which way the body faces the kite while riding, at take-off or landing. */
UENUM(BlueprintType)
enum class ETrickStance : uint8
{
	/** Normal riding: chest to the kite, bar in front. */
	Heelside UMETA(DisplayName = "Heelside"),
	/** A frontside 180 from heelside, bar still in front. */
	Toeside  UMETA(DisplayName = "Toeside"),
	/** A backside 180 from heelside: back to the kite, bar held behind the back and not passed. */
	Blind    UMETA(DisplayName = "Blind")
};

/** Direction of a spin or handle pass. Backside turns the same way as a back roll: the back faces the kite first. */
UENUM(BlueprintType)
enum class ETrickSense : uint8
{
	None      UMETA(DisplayName = "None"),
	Frontside UMETA(DisplayName = "Frontside"),
	Backside  UMETA(DisplayName = "Backside")
};

/** One inversion: rolls are about an axis between Up and Front, flips about Side. */
UENUM(BlueprintType)
enum class ETrickInversion : uint8
{
	/** The chest turns first towards the tail. */
	BackRoll  UMETA(DisplayName = "Back roll"),
	/** The chest turns first towards the nose. */
	FrontRoll UMETA(DisplayName = "Front roll"),
	/** Heelside backflip; unhooked, the tantrum. */
	BackFlip  UMETA(DisplayName = "Backflip"),
	FrontFlip UMETA(DisplayName = "Front flip")
};

/** What kind of kite loop was flown during a jump (classification rules in docs/tricks.md 6.6). */
UENUM(BlueprintType)
enum class ETrickLoopKind : uint8
{
	Kiteloop UMETA(DisplayName = "Kiteloop"),
	/** Started high (8 m or more), kite down to 20 degrees or less, 3 body weights or more of pull. */
	Megaloop UMETA(DisplayName = "Megaloop"),
	/** Flown on the way down with the kite high: a landing aid more than a power loop. */
	HeliLoop UMETA(DisplayName = "Heli loop"),
	/** Half a loop one way, then half the other. */
	SLoop    UMETA(DisplayName = "S-loop")
};

/** When a roll inside a loop started relative to the loop's pull (the yank). */
UENUM(BlueprintType)
enum class ELoopRollTiming : uint8
{
	None  UMETA(DisplayName = "None"),
	Early UMETA(DisplayName = "Early"),
	Late  UMETA(DisplayName = "Late")
};

/** The hand that holds a grab. */
UENUM(BlueprintType)
enum class ETrickHand : uint8
{
	Front UMETA(DisplayName = "Front"),
	Back  UMETA(DisplayName = "Back")
};

/** Where on the board a grab is held (docs/tricks.md 3.2). */
UENUM(BlueprintType)
enum class ETrickGrabZone : uint8
{
	Nose       UMETA(DisplayName = "Nose"),
	/** Toe edge between the feet. */
	ToeEdge    UMETA(DisplayName = "Toe edge"),
	/** Heel edge between the feet. */
	HeelEdge   UMETA(DisplayName = "Heel edge"),
	Tail       UMETA(DisplayName = "Tail"),
	/** Heel edge, reached through or behind the legs. */
	BehindHeel UMETA(DisplayName = "Behind, heel edge"),
	/** Toe edge, reached through or behind the legs. */
	BehindToe  UMETA(DisplayName = "Behind, toe edge")
};

/** What happened to the board while it was off the feet. */
UENUM(BlueprintType)
enum class ETrickBoardOff : uint8
{
	None      UMETA(DisplayName = "None"),
	Plain     UMETA(DisplayName = "Board-off"),
	Superman  UMETA(DisplayName = "Superman"),
	TicTac    UMETA(DisplayName = "Tic tac"),
	BoardPass UMETA(DisplayName = "Board pass"),
	BoardFlip UMETA(DisplayName = "Board flip")
};

/** Where a handle pass was completed. */
UENUM(BlueprintType)
enum class ETrickPassKind : uint8
{
	None    UMETA(DisplayName = "None"),
	/** Completed in the air. */
	Air     UMETA(DisplayName = "Air"),
	/** Completed on or just after touchdown. */
	Surface UMETA(DisplayName = "Surface")
};

/** One jump's score: Height x (1 + Extremity) x (1 + Technicality) x Execution (docs/tricks.md 6.8). */
USTRUCT(BlueprintType)
struct FTrickScore
{
	GENERATED_BODY()

	/** Apex height in metres raised to FTrickScoringSettings::HeightExponent. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float Height = 0.0f;

	/** From the kite loops: how low the kite went and how late in the jump the loop started. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float Extremity = 0.0f;

	/** Sum of the element points: inversions, spin, grabs, board-off, passes, unhooked, stance. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float Technicality = 0.0f;

	/** The landing grade's factor: 1 for stomped down to 0 for a crash. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float Execution = 0.0f;

	/** The product, before any repeat factor. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float Total = 0.0f;
};
