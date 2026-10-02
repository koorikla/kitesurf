#pragma once

#include "CoreMinimal.h"

/**
 * The one place the simulation's units and physical constants are defined.
 *
 * Unreal positions are centimetres and forces on components are kg*cm/s^2. The aerodynamics
 * and hydrodynamics are done in SI (m, kg, s, N) and converted once at the boundary with the
 * helpers here. Wind and board speeds are shown to the player in knots.
 */
namespace KiteUnits
{
	constexpr float CmPerM = 100.0f;

	/** 1 knot = 0.5144 m/s = 51.44 cm/s. */
	constexpr float CmPerKnot = 51.44f;

	/** 1 N = 1 kg*m/s^2 = 100 kg*cm/s^2, the force unit a component in cm feels. */
	constexpr float UnrealForcePerN = 100.0f;

	constexpr float GravityMS2 = 9.81f;
	constexpr float GravityCmS2 = GravityMS2 * CmPerM;

	constexpr float AirDensityKgM3 = 1.225f;
	constexpr float WaterDensityKgM3 = 1025.0f;

	constexpr float KnotsToCmS(float Knots) { return Knots * CmPerKnot; }
	constexpr float CmSToKnots(float CmS) { return CmS / CmPerKnot; }
	constexpr float KnotsToMS(float Knots) { return Knots * CmPerKnot / CmPerM; }
	constexpr float MToCm(float M) { return M * CmPerM; }
	constexpr float CmToM(float Cm) { return Cm / CmPerM; }
	constexpr float NToUnrealForce(float N) { return N * UnrealForcePerN; }
	constexpr float UnrealForceToN(float F) { return F / UnrealForcePerN; }
}
