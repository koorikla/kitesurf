#pragma once

#include "CoreMinimal.h"
#include "KiteSurfUnits.h"

/**
 * Landing physics shared by the board (physics phase 2 item 4, docs/physics/plan-2.md) and the
 * jump recorder (docs/tricks/T0.md section 4). Pure, header only.
 */
namespace LandingMath
{
	/**
	 * Default vertical distance a landing is absorbed over: knees and the board into the water (cm).
	 * An estimate. The board's tunable keeps the shared name LandingAbsorbDistanceCm and this value.
	 */
	constexpr float DefaultLandingAbsorbDistanceCm = 30.0f;

	/**
	 * Deceleration of a landing in g: 1 + v^2 / (2 g s), a constant deceleration that stops a sink
	 * rate v over the absorb distance s, plus the rider's weight. Computed in SI. A sink rate at or
	 * under zero gives 1 (just the weight); the absorb distance is held to at least 1 cm.
	 *
	 * Examples at 30 cm: 2 m/s gives 1.68 g, 6 m/s gives 7.12 g.
	 */
	inline float ComputeLandingG(float SinkRateCmS, float AbsorbDistanceCm)
	{
		const float SinkMS = KiteUnits::CmToM(FMath::Max(SinkRateCmS, 0.0f));
		const float AbsorbM = KiteUnits::CmToM(FMath::Max(AbsorbDistanceCm, 1.0f));
		return 1.0f + SinkMS * SinkMS / (2.0f * KiteUnits::GravityMS2 * AbsorbM);
	}
}
