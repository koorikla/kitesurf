#include "Tricks/BoardGrabPoints.h"

// Inside BoardGrabPoints, so in a unity build it cannot meet a helper of the same name from another file.
namespace BoardGrabPoints
{
namespace
{
	FVector ToVector(const FBoardStancePointCm& Point)
	{
		return FVector(Point.X, Point.Y, Point.Z);
	}
}
}

float BoardGrabPoints::HalfWidthCm(float X)
{
	const float S = X / BoardHalfLengthCm;
	if (FMath::Abs(S) > 1.0f)
	{
		return 0.0f;
	}
	const float S2 = S * S;
	return BoardHalfWidthCm * (1.0f - 0.3f * S2 - 0.1f * S2 * S2);
}

float BoardGrabPoints::RockerCm(float X)
{
	const float S = X / BoardHalfLengthCm;
	return BoardRockerCm * S * S;
}

float BoardGrabPoints::DeckTopCm(float X, float Y)
{
	const float HalfWidth = HalfWidthCm(X);
	const float V = HalfWidth > KINDA_SMALL_NUMBER ? FMath::Clamp(Y / HalfWidth, -1.0f, 1.0f) : 1.0f;
	return RockerCm(X) + BoardThicknessHalfCm * (1.0f - 0.5f * V * V);
}

FVector BoardGrabPoints::StanceSocketFor(ETrickGrabZone Zone, ETrickHand Hand)
{
	// The front hand's foot is towards the nose (+X), the back hand's towards the tail.
	const float Spread = Hand == ETrickHand::Front ? EdgeHandSpreadCm : -EdgeHandSpreadCm;
	switch (Zone)
	{
	case ETrickGrabZone::Nose:
		return ToVector(Nose);
	case ETrickGrabZone::Tail:
		return ToVector(Tail);
	case ETrickGrabZone::ToeEdge:
		return ToVector(ToeEdge) + FVector(Spread, 0.0f, 0.0f);
	case ETrickGrabZone::HeelEdge:
		return ToVector(HeelEdge) + FVector(Spread, 0.0f, 0.0f);
	case ETrickGrabZone::BehindToe:
		return ToVector(BehindToe);
	case ETrickGrabZone::BehindHeel:
		return ToVector(BehindHeel);
	default:
		return ToVector(Handle);
	}
}

FVector BoardGrabPoints::ToBoardLocal(const FVector& Stance, float NoseSideSign, float LengthScale)
{
	// The toe edge is the rail in front of the chest: the board's -Y with the nose on the rider's right.
	const float ToeEdgeSign = NoseSideSign >= 0.0f ? -1.0f : 1.0f;
	return FVector(Stance.X * LengthScale, Stance.Y * ToeEdgeSign, Stance.Z);
}

FVector BoardGrabPoints::ToBoardLocal(const FBoardStancePointCm& Stance, float NoseSideSign, float LengthScale)
{
	return ToBoardLocal(ToVector(Stance), NoseSideSign, LengthScale);
}

FVector BoardGrabPoints::SocketFor(ETrickGrabZone Zone, ETrickHand Hand, float NoseSideSign, float LengthScale)
{
	return ToBoardLocal(StanceSocketFor(Zone, Hand), NoseSideSign, LengthScale);
}

FVector BoardGrabPoints::SocketWorld(const FTransform& BoardTransform, ETrickGrabZone Zone, ETrickHand Hand, float NoseSideSign, float LengthScale)
{
	return BoardTransform.TransformPosition(SocketFor(Zone, Hand, NoseSideSign, LengthScale));
}
