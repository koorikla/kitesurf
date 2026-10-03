#pragma once

#include "CoreMinimal.h"
#include "Tricks/TrickSignature.h"

/** The take-off move a freestyle name is keyed on, derived from a signature. */
enum class ETrickMove : uint8
{
	/** No inversion, raley or S-bend. */
	Pop,
	BackRoll,
	/** From a toeside take-off this is the crow mobe and dum dum move. */
	FrontRoll,
	FrontFlip,
	/** Unhooked, the tantrum. */
	BackFlip,
	Raley,
	SBend
};

/**
 * Names a jump from its signature (docs/tricks.md 3.3 and 6.6). Pure functions, sentence case:
 * only the first letter of a name is a capital, except names that are written otherwise (KGB, 313).
 *
 * - Big air (hooked, no pass) composes: [Switch|Toeside|Blind] [Double|Triple] [Early|Late]
 *   {Megaloop|Kiteloop|Contra loop|Heli loop|S-loop} [Double|Triple] {Back roll|Front roll|
 *   Backflip|Front flip} [Frontside|Backside N] [One-footer] [Board-off|Superman|Tic tac|
 *   Board pass|Board flip] [grab] [to blind|to toeside]. Nothing at all is "Straight air".
 * - Freestyle (unhooked or any pass) comes from one table keyed by take-off stance, move, sense,
 *   degrees, landing stance and pass: KGB 5, 313, 315, Slim 7, Back to blind, Frontside 3.
 * - Anything else is described part by part: "Back roll + backside 540 pass to blind", with at
 *   most MaxNamedParts parts before an ellipsis.
 */
namespace TrickNaming
{
	/** Most parts a composed or described name shows; past it the description ends in an ellipsis (U+2026). */
	constexpr int32 MaxNamedParts = 6;

	/** The name shown to the player. */
	KITESURF_API FString Name(const FTrickSignature& Signature);

	/**
	 * What counts as the same trick for repeats: the signature without grab hold times, board-off
	 * time or grade, with pass rotations rounded to half turns. Canonical, not for display.
	 */
	KITESURF_API FString FamilyKey(const FTrickSignature& Signature);

	/** The part-by-part description used when no name fits, for example "Back roll + backside 540 pass to blind". */
	KITESURF_API FString Describe(const FTrickSignature& Signature);

	/** Grab name for a hand and zone (docs/tricks.md 3.2), capitalised: Indy, Melon, Canadian bacon. */
	KITESURF_API FString GrabName(ETrickHand Hand, ETrickGrabZone Zone);

	/** The number a rotation goes by in a pass name: 180 is "1", 360 "3", 540 "5", 1080 "10". Rounded to half turns; empty under 90. */
	KITESURF_API FString PassNumber(int32 Degrees);

	/** True when the signature is named from the freestyle table: unhooked, or any handle pass. */
	KITESURF_API bool IsFreestyle(const FTrickSignature& Signature);

	/** The take-off move: S-bend, then raley, then the first inversion, otherwise a pop. */
	KITESURF_API ETrickMove FreestyleMove(const FTrickSignature& Signature);
}
