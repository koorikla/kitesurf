#include "KiteGear.h"

const TCHAR* KiteGear::GetDisplayName(EKiteModel Model)
{
	return Model == EKiteModel::Boost ? TEXT("BOOST (5 STRUT)") : TEXT("LOOP (3 STRUT)");
}

const TCHAR* KiteGear::GetDescription(EKiteModel Model)
{
	return Model == EKiteModel::Boost
		? TEXT("More lift and glide: higher jumps and longer hangtime. Slower to turn and loop.")
		: TEXT("Light and quick: tight, fast loops and a kite that climbs back to catch you.");
}

EKiteModel KiteGear::Next(EKiteModel Model)
{
	return KiteModelFromIndex((static_cast<int32>(Model) + 1) % static_cast<int32>(EKiteModel::Count));
}

EKiteModel KiteGear::KiteModelFromIndex(int32 Index)
{
	return (Index >= 0 && Index < static_cast<int32>(EKiteModel::Count)) ? static_cast<EKiteModel>(Index) : EKiteModel::Loop;
}

FKiteModelTraits KiteGear::GetTraits(EKiteModel Model)
{
	FKiteModelTraits Traits;
	if (Model == EKiteModel::Boost)
	{
		// Two more struts hold a flatter, more efficient canopy that carries more load and glides
		// further, at the price of weight and a wider turn.
		Traits.TurnRadiusScale = 1.3f;
		Traits.LiftScale = 1.12f;
		Traits.InducedDragScale = 0.85f;
		Traits.MassScale = 1.2f;
	}
	return Traits;
}

const TCHAR* KiteGear::GetDisplayName(EBoardSize Size)
{
	switch (Size)
	{
	case EBoardSize::Small:
		return TEXT("132 CM");
	case EBoardSize::Large:
		return TEXT("145 CM");
	default:
		return TEXT("138 CM");
	}
}

const TCHAR* KiteGear::GetDescription(EBoardSize Size)
{
	switch (Size)
	{
	case EBoardSize::Small:
		return TEXT("Poppy and quick to turn. Needs more speed to plane and sinks sooner in light wind.");
	case EBoardSize::Large:
		return TEXT("Planes early and grips hard: the light-wind board. Less pop, slower turns.");
	default:
		return TEXT("The all-rounder.");
	}
}

EBoardSize KiteGear::Next(EBoardSize Size)
{
	return BoardSizeFromIndex((static_cast<int32>(Size) + 1) % static_cast<int32>(EBoardSize::Count));
}

EBoardSize KiteGear::BoardSizeFromIndex(int32 Index)
{
	return (Index >= 0 && Index < static_cast<int32>(EBoardSize::Count)) ? static_cast<EBoardSize>(Index) : EBoardSize::Medium;
}

FBoardSizeTraits KiteGear::GetTraits(EBoardSize Size)
{
	FBoardSizeTraits Traits;
	if (Size == EBoardSize::Small)
	{
		Traits.PopScale = 1.2f;
		Traits.PlaningSpeedScale = 1.2f;
		Traits.PlaningDragScale = 0.9f;
		Traits.GripScale = 0.88f;
		Traits.TurnRateScale = 1.2f;
	}
	else if (Size == EBoardSize::Large)
	{
		Traits.PopScale = 0.85f;
		Traits.PlaningSpeedScale = 0.82f;
		Traits.PlaningDragScale = 1.12f;
		Traits.GripScale = 1.15f;
		Traits.TurnRateScale = 0.85f;
	}
	return Traits;
}
