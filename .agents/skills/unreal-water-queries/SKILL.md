---
name: unreal-water-queries
description: Query Unreal Water plugin wave height and normals from C++.
version: 0.1.0
metadata:
  hermes:
    tags: [unreal-engine, water, waves, gerstner, physics]
    related_skills: [kitesurf-big-air-sim, kitesurf-automation-tests, unreal-cpp-gameplay]
---

# Unreal Water plugin queries

How gameplay code reads the ocean surface (height, normal, velocity) from the Water
plugin in Unreal Engine 5.8, and how to keep that testable. Signatures below were read from
the 5.8.3 headers; their runtime behaviour has not been exercised in this project yet.

## When to Use

- Making the board follow waves, take off from a kicker, or land on a moving surface.
- Sampling the water under several points of the board.
- Anything that needs the rendered ocean and the simulated ocean to agree.

## Procedure

1. **Add the dependency.** `Water` is already in `KiteSurf.Build.cs` and enabled in
   `KiteSurf.uproject`. The plugin is marked experimental in 5.8.
2. **Find the water body** once and keep a weak reference to its `UWaterBodyComponent`
   (via `AWaterBody::GetWaterBodyComponent()`); do not search the world every tick.
3. **Query with waves included.**
   ```cpp
   #include "WaterBodyComponent.h"
   #include "WaterBodyTypes.h"

   const EWaterBodyQueryFlags Flags =
       EWaterBodyQueryFlags::ComputeLocation |
       EWaterBodyQueryFlags::ComputeNormal |
       EWaterBodyQueryFlags::ComputeVelocity |
       EWaterBodyQueryFlags::IncludeWaves;

   auto Result = WaterBody->TryQueryWaterInfoClosestToWorldLocation(WorldLocation, Flags);
   if (Result.HasValue())
   {
       const FWaterBodyQueryResult& Info = Result.GetValue();
       // surface location, normal and velocity are on Info
   }
   ```
   `TryQueryWaterInfoClosestToWorldLocation` returns
   `TValueOrError<FWaterBodyQueryResult, EWaterBodyQueryError>`; handle the error case.
   The older `QueryWaterInfoClosestToWorldLocation` is deprecated since 5.7.
4. **Sample several points under the board** (nose, tail, both rails, centre), fit a plane
   for the surface normal, and use the vertical speed of the surface relative to the board
   for slam and pop.
5. **Hide the plugin behind a project interface** with two implementations: one that calls
   the Water plugin, and one analytic surface (flat, or a small set of Gerstner waves) used
   by automation tests, which run without a water body.
6. **Author kickers as wave settings**, not geometry: a few long, steep waves aligned with
   the wind in the ocean's wave asset. Gerstner waves do not break.

## Pitfalls

- **Leaving waves out.** `GetWaterSurfaceInfoAtLocation` returns the flat rest surface;
  the current board code uses it, so the board ignores waves. Use the flags above.
- **Calling the raw wave generator.** `UGerstnerWaterWaves::GetWaveHeightAtPosition`
  skips depth attenuation and may not match the rendered surface. Go through the water
  body component.
- **Assuming thread safety.** Treat these queries as game-thread only.
- **Using `UBuoyancyComponent` for the board.** It models displaced volume on pontoons,
  not planing lift. It is fine for a kite floating on the water or for buoys.
- **Mismatched time.** Wave phase depends on the water subsystem's time; a test that
  compares against the plugin must use the same time source.
- **Rendering artifacts on Vulkan.** Single Layer Water artifacts were confirmed by Epic
  for 5.6 and 5.7 on Vulkan SM6, with `r.Water.SingleLayer.DepthPrepass=0` as the
  workaround. Whether 5.8.3 needs it is untested here.

## Verification

- An automation test compares the plugin implementation and the analytic implementation at
  a grid of points and reports the largest difference.
- In a `-game` run the board visibly rises and falls with the swell.
- The query count per frame is stated (points sampled times bodies), with a measured cost
  if it grows.
