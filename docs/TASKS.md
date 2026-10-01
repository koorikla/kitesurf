# Development Tasks Roadmap

1. **Environment**:
   - Install prebuilt Unreal Engine 5.8 Linux binaries to `/opt/unreal-engine`.
   - Setup project compilation and Vulkan SM6 rendering pipeline.

2. **Wind & Weather**:
   - Implement spatial gust fields and wind variation in `UWindComponent`.
   - Add visual wind cues (surface water ripples, particle effects).

3. **Kite Aerodynamics**:
   - Replace placeholder physics with realistic 2D/3D leading edge inflatable (LEI) kite aero model.
   - Implement depower / trim sheeting response and wind window power zones.

4. **Board Hydrodynamics**:
   - Implement twin-tip board water resistance, edge carving, and spray.
   - Integrate with UE Water plugin buoyancy and surface waves.

5. **Input & Camera**:
   - Configure Enhanced Input mappings for gamepad and keyboard/mouse.
   - Tune dynamic third-person chase camera and kite tracking angles.

6. **HUD & Telemetry**:
   - Implement speedometer (knots), wind direction compass, and kite position indicator.
   - Display player stats and course markers.

7. **Rider Visuals**:
   - Integrate skeletal mesh rider and board with IK leg positioning.
   - Connect line physics and harness attachment rigging.

8. **CI Runner**:
   - Setup self-hosted Linux CI runner with GPU/Vulkan support for headless automated test runs.
