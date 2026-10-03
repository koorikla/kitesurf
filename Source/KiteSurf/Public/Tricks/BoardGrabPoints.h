#pragma once

#include "CoreMinimal.h"
#include "RiderRig.h"
#include "Tricks/TrickTypes.h"

/** A point on the board in stance space (cm): see BoardGrabPoints. A plain struct so the table can be constexpr. */
struct FBoardStancePointCm
{
	float X = 0.0f;
	float Y = 0.0f;
	float Z = 0.0f;
};

/**
 * Where the hands grab the board and where the feet are strapped (docs/tricks/T2.md, T2.0).
 *
 * The table is in "stance space", in cm: X towards the nose (the board's +X, the leading tip; the
 * front foot's side), Y towards the toe edge (the rail in front of the rider's chest), Z up out of
 * the deck. Board-local is (X * LengthScale, Y * ToeEdgeSign, Z) with ToeEdgeSign = -NoseSideSign:
 * with the nose on the rider's right (NoseSideSign +1, RiderAttitudeComponent's convention) the toe
 * edge is the board's -Y.
 *
 * The points are grip points, where the palm closes: on the rail at mid-thickness, or at the tip.
 * They are worked out from SM_KiteBoard's outline as scripts/editor/generate_mesh_objs.py draws it
 * (generate_board_obj): half length 70, half width 21 * (1 - 0.3 s^2 - 0.1 s^4) and rocker 4 s^2
 * with s = x / 70, thickness 2.5 thinning to half at the rails. HalfWidthCm, RockerCm and
 * DeckTopCm mirror those formulas; change them together with the script. The strap loops are at
 * x = +-StrapHalfSpacingCm, the rig's spacing (RiderRig.h), the one constant for both.
 *
 * C++ reads these constants, not mesh sockets, so the tests need no asset and the drawn board
 * needs no mesh.
 */
namespace BoardGrabPoints
{
	/** The rig's strap spacing, the one source of truth (RiderRig.h); the mesh script mirrors it. */
	inline constexpr float StrapHalfSpacingCm = RiderRig::StrapHalfSpacingCm;
	/** How high the ankle sits above the board's origin in a strap (RiderRig.h). */
	inline constexpr float AnkleHeightCm = RiderRig::AnkleHeightCm;

	/** SM_KiteBoard's outline, as generate_mesh_objs.py draws it (cm, at length scale 1). */
	inline constexpr float BoardHalfLengthCm = 70.0f;
	inline constexpr float BoardHalfWidthCm = 21.0f;
	inline constexpr float BoardThicknessHalfCm = 1.25f;
	inline constexpr float BoardRockerCm = 4.0f;

	/** Along the rail, a hand on an edge sits this far towards its own foot (front hand nose-wards), so two hands on one rail do not overlap. An estimate. */
	inline constexpr float EdgeHandSpreadCm = 8.0f;

	// Stance space, cm. Estimates from the outline: tune them with the grab poses (T2.1).
	/** The leading tip, 7 cm in from its end, mid-thickness on the rocker line. */
	inline constexpr FBoardStancePointCm Nose{ 63.0f, 0.0f, 3.24f };
	/** The trailing tip, mirrored. */
	inline constexpr FBoardStancePointCm Tail{ -63.0f, 0.0f, 3.24f };
	/** The toe rail between the feet (half width there is 21). A hand on it moves EdgeHandSpreadCm towards its own foot. */
	inline constexpr FBoardStancePointCm ToeEdge{ 0.0f, 20.0f, 0.0f };
	/** The heel rail between the feet. */
	inline constexpr FBoardStancePointCm HeelEdge{ 0.0f, -20.0f, 0.0f };
	/** The toe rail reached through or behind the legs: between the feet, the same for either hand. */
	inline constexpr FBoardStancePointCm BehindToe{ 0.0f, 20.0f, 0.0f };
	/** The heel rail reached through or behind the legs. */
	inline constexpr FBoardStancePointCm BehindHeel{ 0.0f, -20.0f, 0.0f };
	/** The board-off grip in the middle of the deck (deck top is 1.25 there): where a handle would be. */
	inline constexpr FBoardStancePointCm Handle{ 0.0f, 0.0f, 3.5f };
	/** Two hands on the toe rail for a superman: in front of the front strap and behind the back one. */
	inline constexpr FBoardStancePointCm ToeRailFront{ 32.0f, 19.0f, 0.84f };
	inline constexpr FBoardStancePointCm ToeRailBack{ -32.0f, 19.0f, 0.84f };
	/** The ankles in the straps, where RiderRig::SolveBody puts them. */
	inline constexpr FBoardStancePointCm FrontStrap{ StrapHalfSpacingCm, 0.0f, AnkleHeightCm };
	inline constexpr FBoardStancePointCm BackStrap{ -StrapHalfSpacingCm, 0.0f, AnkleHeightCm };

	/** Half the board's width at X along it (cm, board-local at length scale 1); 0 past the tips. */
	KITESURF_API float HalfWidthCm(float X);
	/** The rocker line, the board's mid-thickness along its middle, at X (cm). */
	KITESURF_API float RockerCm(float X);
	/** The deck top at (X, Y) (cm): the rocker plus a thickness that halves towards the rails. */
	KITESURF_API float DeckTopCm(float X, float Y);

	/** The point's stance-space table entry for a zone and hand: the edge zones move EdgeHandSpreadCm towards the hand's foot. */
	KITESURF_API FVector StanceSocketFor(ETrickGrabZone Zone, ETrickHand Hand);
	/** A stance-space point in board-local cm, for the nose on the rider's NoseSideSign side and a board LengthScale times the reference. */
	KITESURF_API FVector ToBoardLocal(const FVector& Stance, float NoseSideSign, float LengthScale = 1.0f);
	KITESURF_API FVector ToBoardLocal(const FBoardStancePointCm& Stance, float NoseSideSign, float LengthScale = 1.0f);

	/** Where Hand grabs the board in Zone, board-local (cm). */
	KITESURF_API FVector SocketFor(ETrickGrabZone Zone, ETrickHand Hand, float NoseSideSign, float LengthScale = 1.0f);
	/** The same point in the world, on a board at BoardTransform (BoardVisual's component transform in the game). */
	KITESURF_API FVector SocketWorld(const FTransform& BoardTransform, ETrickGrabZone Zone, ETrickHand Hand, float NoseSideSign, float LengthScale = 1.0f);
}
