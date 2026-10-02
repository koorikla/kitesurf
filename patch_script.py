import os
import re

path = 'scripts/editor/generate_mesh_objs.py'

with open(path, 'r') as f:
    content = f.read()

# Increase polycount
content = content.replace("rings=8, segments=12", "rings=16, segments=24")
content = content.replace("segments=10", "segments=24")

# build_rider spines and muscles
build_rider_insert = """
    # Spine
    spine_path = [(-16.0, 0.0, 87.0), (-14.0, 0.0, 107.0), (-14.0, 0.0, 129.0), (-8.0, 0.0, 138.0)]
    add_tube(mesh, 'RiderRobotDark' if robot else body_material, ROBOT_DARK if robot else body_color, spine_path, [3.5, 4.5, 4.5, 2.5], segments=16)

    # Pecs and abs
    for side in (-1.0, 1.0):
        add_ellipsoid(mesh, body_material, body_color, (4.0, side * 7.0, 125.0), (7.0, 10.0, 11.0), rings=12, segments=20)
        add_ellipsoid(mesh, body_material, body_color, (3.0, side * 5.0, 107.0), (7.0, 8.0, 10.0), rings=12, segments=20)

    # Shoulder deltoids and joints
    for side in (-1.0, 1.0):
        shoulder = (-6.0, side * 23.0, 134.0)
        knee = (12.0, side * 29.0, 46.0)
        elbow = (16.0, side * 27.0, 120.0)
        add_ellipsoid(mesh, body_material, body_color, (shoulder[0] + 1.0, shoulder[1] + side * 2.0, shoulder[2] - 2.0), (5.5, 6.5, 7.5), rings=12, segments=20)
"""
if "spine_path = [(-16.0" not in content:
    content = content.replace("    _limb(mesh, 'RiderRobotDark' if robot else 'RiderSkin', ROBOT_DARK if robot else SKIN, [(-5.0, 0.0, 138.0), (-3.0, 0.0, 148.0)], [6.0, 5.5])     # neck", 
                              "    _limb(mesh, 'RiderRobotDark' if robot else 'RiderSkin', ROBOT_DARK if robot else SKIN, [(-5.0, 0.0, 138.0), (-3.0, 0.0, 148.0)], [6.0, 5.5])     # neck\n" + build_rider_insert)

# build_rider_torso spines and muscles
build_rider_torso_insert = """
    # Spine
    spine_path = [at((-16.0, 0.0, 87.0)), at((-14.0, 0.0, 107.0)), at((-14.0, 0.0, 129.0)), at((-8.0, 0.0, 138.0))]
    add_tube(mesh, 'RiderRobotDark' if robot else body_material, ROBOT_DARK if robot else body_color, spine_path, [3.5, 4.5, 4.5, 2.5], segments=16)

    # Pecs and abs
    for side in (-1.0, 1.0):
        add_ellipsoid(mesh, body_material, body_color, at((4.0, side * 7.0, 125.0)), (7.0, 10.0, 11.0), rings=12, segments=20)
        add_ellipsoid(mesh, body_material, body_color, at((3.0, side * 5.0, 107.0)), (7.0, 8.0, 10.0), rings=12, segments=20)
"""
if "spine_path = [at((-16.0" not in content:
    content = content.replace("    _limb(mesh, 'RiderRobotDark' if robot else 'RiderSkin', ROBOT_DARK if robot else SKIN, [at((-5.0, 0.0, 138.0)), at((-3.0, 0.0, 148.0))], [6.0, 5.5])  # neck",
                              "    _limb(mesh, 'RiderRobotDark' if robot else 'RiderSkin', ROBOT_DARK if robot else SKIN, [at((-5.0, 0.0, 138.0)), at((-3.0, 0.0, 148.0))], [6.0, 5.5])  # neck\n" + build_rider_torso_insert)


# build_rider_limb
limb_replacements = [
    (
        "        _limb(mesh, body_material, body_color, [(0.0, 0.0, 0.0), (length, 0.0, 0.0)], [10.5, 7.5])\n        add_ellipsoid(mesh, 'RiderRobotDark' if robot else body_material, ROBOT_DARK if robot else body_color, (length, 0.0, 0.0), (7.8, 7.8, 7.8), rings=6, segments=24)  # knee",
        "        _limb(mesh, body_material, body_color, [(0.0, 0.0, 0.0), (length, 0.0, 0.0)], [10.5, 7.5])\n        add_ellipsoid(mesh, 'RiderRobotDark' if robot else body_material, ROBOT_DARK if robot else body_color, (length, 0.0, 0.0), (8.5, 8.5, 8.5), rings=16, segments=24)  # knee\n        add_ellipsoid(mesh, body_material, body_color, (length * 0.45, 0.0, 3.5), (length * 0.4, 7.0, 8.0), rings=16, segments=24)  # quad muscle\n        add_ellipsoid(mesh, body_material, body_color, (length * 0.5, 0.0, -3.0), (length * 0.35, 6.0, 7.0), rings=16, segments=24)  # hamstring"
    ),
    (
        "        _limb(mesh, body_material, body_color, [(0.0, 0.0, 0.0), (length, 0.0, 0.0)], [7.5, 5.5])\n        # The foot, pointing the way the knee bends.\n        add_ellipsoid(mesh, 'RiderRobotDark' if robot else 'RiderSkin', ROBOT_DARK if robot else SKIN, (length + 1.0, 0.0, 6.0), (4.5, 6.0, 13.0))",
        "        _limb(mesh, body_material, body_color, [(0.0, 0.0, 0.0), (length, 0.0, 0.0)], [7.5, 5.5])\n        add_ellipsoid(mesh, body_material, body_color, (length * 0.3, 0.0, -2.5), (length * 0.25, 5.5, 6.5), rings=16, segments=24)  # calf muscle\n        # The foot, pointing the way the knee bends.\n        add_ellipsoid(mesh, 'RiderRobotDark' if robot else 'RiderSkin', ROBOT_DARK if robot else SKIN, (length + 1.0, 0.0, 6.0), (5.5, 7.0, 14.0), rings=16, segments=24)"
    ),
    (
        "        add_ellipsoid(mesh, 'RiderRobotDark' if robot else body_material, ROBOT_DARK if robot else body_color, (0.0, 0.0, 0.0), (7.5, 7.5, 7.5), rings=6, segments=24)  # shoulder\n        _limb(mesh, body_material, body_color, [(0.0, 0.0, 0.0), (length, 0.0, 0.0)], [6.5, 5.5])",
        "        add_ellipsoid(mesh, 'RiderRobotDark' if robot else body_material, ROBOT_DARK if robot else body_color, (0.0, 0.0, 0.0), (8.5, 8.5, 8.5), rings=16, segments=24)  # shoulder\n        _limb(mesh, body_material, body_color, [(0.0, 0.0, 0.0), (length, 0.0, 0.0)], [6.5, 5.5])\n        add_ellipsoid(mesh, body_material, body_color, (length * 0.45, 0.0, 1.5), (length * 0.35, 5.0, 6.0), rings=16, segments=24)  # bicep\n        add_ellipsoid(mesh, body_material, body_color, (length * 0.5, 0.0, -1.5), (length * 0.35, 4.5, 5.5), rings=16, segments=24)  # tricep"
    ),
    (
        "        add_ellipsoid(mesh, 'RiderRobotDark' if robot else body_material, ROBOT_DARK if robot else body_color, (0.0, 0.0, 0.0), (5.6, 5.6, 5.6), rings=6, segments=24)  # elbow\n        _limb(mesh, body_material, body_color, [(0.0, 0.0, 0.0), (length, 0.0, 0.0)], [5.5, 4.5])\n        add_ellipsoid(mesh, 'RiderRobotDark' if robot else 'RiderSkin', ROBOT_DARK if robot else SKIN, (length, 0.0, 0.0), (5.5, 5.0, 5.0), rings=6, segments=24)       # hand",
        "        add_ellipsoid(mesh, 'RiderRobotDark' if robot else body_material, ROBOT_DARK if robot else body_color, (0.0, 0.0, 0.0), (7.0, 7.0, 7.0), rings=16, segments=24)  # elbow\n        _limb(mesh, body_material, body_color, [(0.0, 0.0, 0.0), (length, 0.0, 0.0)], [5.5, 4.5])\n        add_ellipsoid(mesh, body_material, body_color, (length * 0.3, 0.0, 1.0), (length * 0.25, 4.5, 5.0), rings=16, segments=24)  # forearm muscle\n        add_ellipsoid(mesh, 'RiderRobotDark' if robot else 'RiderSkin', ROBOT_DARK if robot else SKIN, (length, 0.0, 0.0), (6.0, 5.5, 5.5), rings=16, segments=24)       # hand"
    )
]

for old, new in limb_replacements:
    content = content.replace(old, new)

with open(path, 'w') as f:
    f.write(content)
print("done")
