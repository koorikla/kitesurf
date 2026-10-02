"""Procedural geometry for the kite, the board, the control bar and the riders.

Pure Python (no Unreal imports), so it runs under the editor's Python and under a plain
interpreter. scripts/editor/import_geometry.py calls generate_all() and imports the results.

Mesh frames, in centimetres:
  Kite:   X towards the leading edge, Y along the span, Z out along the lines (away from the rider).
  Riders: X is the way the rider faces (towards the kite), Y to their right, Z up, origin between the feet.
"""
import math
import os


class Mesh:
    """Triangle soup with one material name per triangle; writes OBJ + MTL with smooth normals per part."""

    def __init__(self):
        self.vertices = []   # (x, y, z)
        self.uvs = []        # (u, v)
        self.normals = []    # (x, y, z)
        self.triangles = {}  # material -> [(i0, i1, i2)] zero-based
        self.colors = {}     # material -> (r, g, b) for the MTL preview colour

    def add_part(self, material, color, positions, uvs, triangles, double_sided=False):
        """Adds a part with its own vertices; normals are averaged over the part's triangles."""
        self.colors[material] = color
        sides = [1.0, -1.0] if double_sided else [1.0]
        for side in sides:
            base = len(self.vertices)
            normals = [[0.0, 0.0, 0.0] for _ in positions]
            ordered = [(a, b, c) if side > 0 else (a, c, b) for a, b, c in triangles]
            for a, b, c in ordered:
                pa, pb, pc = positions[a], positions[b], positions[c]
                ux, uy, uz = pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]
                vx, vy, vz = pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]
                nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
                for index in (a, b, c):
                    normals[index][0] += nx
                    normals[index][1] += ny
                    normals[index][2] += nz
            for position, uv, normal in zip(positions, uvs, normals):
                length = math.sqrt(sum(n * n for n in normal)) or 1.0
                self.vertices.append(position)
                self.uvs.append(uv)
                self.normals.append((normal[0] / length, normal[1] / length, normal[2] / length))
            self.triangles.setdefault(material, []).extend((a + base, b + base, c + base) for a, b, c in ordered)

    def write(self, obj_path):
        mtl_path = os.path.splitext(obj_path)[0] + '.mtl'
        with open(mtl_path, 'w') as f:
            for material, color in self.colors.items():
                f.write(f'newmtl {material}\nKd {color[0]:.3f} {color[1]:.3f} {color[2]:.3f}\n\n')
        with open(obj_path, 'w') as f:
            f.write(f'mtllib {os.path.basename(mtl_path)}\n')
            for v in self.vertices:
                f.write(f'v {v[0]:.4f} {v[1]:.4f} {v[2]:.4f}\n')
            for vt in self.uvs:
                f.write(f'vt {vt[0]:.4f} {vt[1]:.4f}\n')
            for vn in self.normals:
                f.write(f'vn {vn[0]:.4f} {vn[1]:.4f} {vn[2]:.4f}\n')
            for material, triangles in self.triangles.items():
                f.write(f'usemtl {material}\n')
                for a, b, c in triangles:
                    f.write(f'f {a + 1}/{a + 1}/{a + 1} {b + 1}/{b + 1}/{b + 1} {c + 1}/{c + 1}/{c + 1}\n')


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def _scale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _normalize(a):
    length = math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]) or 1.0
    return (a[0] / length, a[1] / length, a[2] / length)


def _frame(direction):
    """Two unit vectors perpendicular to direction and to each other."""
    direction = _normalize(direction)
    helper = (0.0, 0.0, 1.0) if abs(direction[2]) < 0.9 else (1.0, 0.0, 0.0)
    side = _normalize(_cross(helper, direction))
    up = _cross(direction, side)
    return side, up


def add_tube(mesh, material, color, path, radii, segments=10, capped=True):
    """A tube through the points of path with a radius at each point."""
    positions, uvs, triangles = [], [], []
    count = len(path)
    for i, point in enumerate(path):
        tangent = _sub(path[min(i + 1, count - 1)], path[max(i - 1, 0)])
        side, up = _frame(tangent)
        for j in range(segments):
            angle = 2.0 * math.pi * j / segments
            offset = _add(_scale(side, math.cos(angle) * radii[i]), _scale(up, math.sin(angle) * radii[i]))
            positions.append(_add(point, offset))
            uvs.append((i / max(count - 1, 1), j / segments))
    for i in range(count - 1):
        for j in range(segments):
            a = i * segments + j
            b = i * segments + (j + 1) % segments
            c = (i + 1) * segments + (j + 1) % segments
            d = (i + 1) * segments + j
            triangles.append((a, c, b))
            triangles.append((a, d, c))
    if capped:
        for end, ring_start, flip in ((0, 0, False), (count - 1, (count - 1) * segments, True)):
            centre = len(positions)
            positions.append(path[end])
            uvs.append((0.5, 0.5))
            for j in range(segments):
                a = ring_start + j
                b = ring_start + (j + 1) % segments
                triangles.append((centre, a, b) if flip else (centre, b, a))
    mesh.add_part(material, color, positions, uvs, triangles)


def add_ellipsoid(mesh, material, color, centre, radii, rings=8, segments=12):
    positions, uvs, triangles = [], [], []
    for i in range(rings + 1):
        phi = math.pi * i / rings
        for j in range(segments):
            theta = 2.0 * math.pi * j / segments
            positions.append((
                centre[0] + radii[0] * math.sin(phi) * math.cos(theta),
                centre[1] + radii[1] * math.sin(phi) * math.sin(theta),
                centre[2] + radii[2] * math.cos(phi)))
            uvs.append((j / segments, i / rings))
    for i in range(rings):
        for j in range(segments):
            a = i * segments + j
            b = i * segments + (j + 1) % segments
            c = (i + 1) * segments + (j + 1) % segments
            d = (i + 1) * segments + j
            triangles.append((a, c, b))
            triangles.append((a, d, c))
    mesh.add_part(material, color, positions, uvs, triangles)


# ----------------------------------------------------------------------------------------------
# Kite: a three-strut delta-hybrid leading edge inflatable, about 12 m2.
# ----------------------------------------------------------------------------------------------

KITE_ARC_RADIUS = 225.0        # the canopy bends round an arc of this radius, seen from in front
KITE_ARC_HALF_ANGLE = 78.0     # degrees from the centre of the arc to each wingtip
KITE_CENTRE_CHORD = 235.0
KITE_TIP_CHORD = 95.0
KITE_TIP_SWEEP = 70.0          # how far the leading edge sweeps back at the tips
KITE_STRUT_POSITIONS = (-0.52, 0.0, 0.52)
# The boost kite: the same outline (so the lines meet it at the same wingtips) with five struts.
KITE_BOOST_STRUT_POSITIONS = (-0.68, -0.34, 0.0, 0.34, 0.68)


def kite_section(s):
    """Leading-edge point, chord, and outward normal of the kite at span position s in [-1, 1]."""
    theta = math.radians(KITE_ARC_HALF_ANGLE) * s
    chord = KITE_CENTRE_CHORD + (KITE_TIP_CHORD - KITE_CENTRE_CHORD) * abs(s) ** 1.7
    leading_edge = (
        -KITE_TIP_SWEEP * abs(s) ** 1.6,
        KITE_ARC_RADIUS * math.sin(theta),
        KITE_ARC_RADIUS * (math.cos(theta) - 1.0))
    normal = (0.0, math.sin(theta), math.cos(theta))
    return leading_edge, chord, normal


def kite_surface_point(s, v, lift=0.0):
    """Point on the canopy at span s and chord fraction v (0 = leading edge), with its camber."""
    leading_edge, chord, normal = kite_section(s)
    camber = 0.085 * chord * math.sin(math.pi * v ** 0.75)
    return (
        leading_edge[0] - v * chord,
        leading_edge[1] + normal[1] * (camber + lift),
        leading_edge[2] + normal[2] * (camber + lift))


def kite_wingtip(left):
    """Where a steering line meets the kite: the trailing corner of the wingtip."""
    s = -1.0 if left else 1.0
    return kite_surface_point(s, 0.85)


def build_kite(strut_positions=KITE_STRUT_POSITIONS, canopy_material='KiteCanopy'):
    mesh = Mesh()
    white = (0.93, 0.93, 0.93)

    # Canopy: the texture carries the colour blocking and the wordmark (u along the span, v along the chord).
    span_segments, chord_segments = 32, 10
    positions, uvs, triangles = [], [], []
    for i in range(span_segments + 1):
        s = -1.0 + 2.0 * i / span_segments
        for j in range(chord_segments + 1):
            v = j / chord_segments
            positions.append(kite_surface_point(s, v))
            uvs.append((i / span_segments, v))
    for i in range(span_segments):
        for j in range(chord_segments):
            a = i * (chord_segments + 1) + j
            b = (i + 1) * (chord_segments + 1) + j
            c = b + 1
            d = a + 1
            triangles.append((a, b, c))
            triangles.append((a, c, d))
    mesh.add_part(canopy_material, (0.2, 0.75, 0.45), positions, uvs, triangles, double_sided=True)

    # Leading edge tube, fat in the middle and tapering to the tips.
    path, radii = [], []
    for i in range(span_segments + 1):
        s = -1.0 + 2.0 * i / span_segments
        leading_edge, _, _ = kite_section(s)
        path.append(leading_edge)
        radii.append(11.0 - 6.0 * abs(s) ** 1.3)
    add_tube(mesh, 'KiteTube', white, path, radii, segments=10)

    # Struts run from the leading edge to the trailing edge just under the canopy.
    for s in strut_positions:
        strut_path, strut_radii = [], []
        for j in range(9):
            v = j / 8.0
            strut_path.append(kite_surface_point(s, v, lift=-4.0))
            strut_radii.append(5.5 - 3.0 * v)
        add_tube(mesh, 'KiteTube', white, strut_path, strut_radii, segments=8)
    return mesh


# ----------------------------------------------------------------------------------------------
# Riders: posed in a kitesurfing stance from simple rounded parts.
# ----------------------------------------------------------------------------------------------

SKIN = (0.93, 0.68, 0.55)
RED = (0.78, 0.05, 0.06)
WHITE = (0.96, 0.96, 0.96)
BLACK = (0.03, 0.03, 0.03)
WETSUIT = (0.05, 0.07, 0.10)
ACCENT = (0.1, 0.75, 0.9)


def _limb(mesh, material, color, points, radii):
    add_tube(mesh, material, color, points, radii, segments=10)


def build_rider(variant):
    """The rider as one piece in a riding pose, used where it only has to be looked at (the gear
    screen's preview). In the ride itself the jointed parts below are posed every frame.

    variant: 'santa' (shirtless, swim trunks, hat and beard) or 'wetsuit'."""
    santa = variant == 'santa'
    mesh = Mesh()
    body_material, body_color = ('RiderSkin', SKIN) if santa else ('RiderWetsuit', WETSUIT)
    belly = 1.0 if santa else 0.72

    for side in (-1.0, 1.0):
        # Legs: feet wide apart across the board, knees bent, hips set back.
        foot = (4.0, side * 30.0, 5.0)
        knee = (12.0, side * 29.0, 46.0)
        hip = (-10.0, side * 13.0, 82.0)
        add_ellipsoid(mesh, 'RiderSkin', SKIN, (foot[0] + 6.0, foot[1], 4.0), (13.0, 6.0, 4.5))
        _limb(mesh, body_material, body_color, [foot, knee], [5.5, 7.5])
        _limb(mesh, body_material, body_color, [knee, hip], [7.5, 10.5 * (0.9 + 0.1 * belly)])
        add_ellipsoid(mesh, body_material, body_color, knee, (7.8, 7.8, 7.8), rings=6, segments=10)

        # Arms: reaching forward to the bar.
        shoulder = (-6.0, side * 23.0, 134.0)
        elbow = (16.0, side * 27.0, 120.0)
        hand = (37.0, side * 24.0, 104.0)
        _limb(mesh, body_material, body_color, [shoulder, elbow], [6.5, 5.5])
        _limb(mesh, body_material, body_color, [elbow, hand], [5.5, 4.5])
        add_ellipsoid(mesh, body_material, body_color, shoulder, (7.5, 7.5, 7.5), rings=6, segments=10)
        add_ellipsoid(mesh, 'RiderSkin', SKIN, hand, (5.5, 5.0, 5.0), rings=6, segments=10)

        if santa:
            # White trim on the hem of each trouser leg.
            hem = (knee[0] + (hip[0] - knee[0]) * 0.45, knee[1] + (hip[1] - knee[1]) * 0.45, knee[2] + (hip[2] - knee[2]) * 0.45)
            add_ellipsoid(mesh, 'RiderWhite', WHITE, hem, (10.5, 10.5, 3.2), rings=6, segments=12)
            upper = (knee[0] + (hip[0] - knee[0]) * 0.72, knee[1] + (hip[1] - knee[1]) * 0.72, knee[2] + (hip[2] - knee[2]) * 0.72)
            add_ellipsoid(mesh, 'RiderRed', RED, upper, (11.5, 11.5, 9.0), rings=6, segments=12)

    # Trunk
    if santa:
        add_ellipsoid(mesh, 'RiderRed', RED, (-9.0, 0.0, 86.0), (19.0, 24.0, 14.0))        # swim trunks
        add_ellipsoid(mesh, 'RiderWhite', WHITE, (-8.0, 0.0, 96.0), (20.5, 25.0, 3.5))     # waistband trim
        add_ellipsoid(mesh, 'RiderBlack', BLACK, (-7.0, 0.0, 91.0), (20.2, 25.2, 2.2))     # belt
        add_ellipsoid(mesh, 'RiderSkin', SKIN, (-2.0, 0.0, 109.0), (24.0, 25.0, 21.0))     # belly
    else:
        add_ellipsoid(mesh, body_material, body_color, (-9.0, 0.0, 87.0), (16.0, 20.0, 14.0))
        add_ellipsoid(mesh, body_material, body_color, (-6.0, 0.0, 107.0), (15.0, 19.0, 19.0))
        add_ellipsoid(mesh, 'RiderAccent', ACCENT, (-6.0, 0.0, 99.0), (15.6, 19.6, 3.0))   # harness
    add_ellipsoid(mesh, body_material, body_color, (-6.0, 0.0, 129.0), (17.0, 23.0, 15.0))  # chest
    _limb(mesh, 'RiderSkin', SKIN, [(-5.0, 0.0, 138.0), (-3.0, 0.0, 148.0)], [6.0, 5.5])     # neck

    # Head
    head = (-2.0, 0.0, 156.0)
    add_ellipsoid(mesh, 'RiderSkin', SKIN, head, (11.5, 10.5, 12.5), rings=10, segments=14)
    add_ellipsoid(mesh, 'RiderSkin', SKIN, (9.5, 0.0, 156.0), (3.0, 2.6, 2.6), rings=6, segments=8)   # nose
    for side in (-1.0, 1.0):
        add_ellipsoid(mesh, 'RiderBlack', BLACK, (8.0, side * 4.2, 160.0), (1.6, 1.6, 1.8), rings=6, segments=8)  # eyes

    if santa:
        # Beard, moustache, and a hat with a trim band and a pom-pom on its drooping tip.
        add_ellipsoid(mesh, 'RiderWhite', WHITE, (5.0, 0.0, 147.5), (9.5, 11.0, 10.5))
        add_ellipsoid(mesh, 'RiderWhite', WHITE, (2.0, 0.0, 140.0), (8.0, 8.5, 9.0))
        for side in (-1.0, 1.0):
            add_ellipsoid(mesh, 'RiderWhite', WHITE, (9.5, side * 4.0, 152.5), (2.6, 4.2, 1.8), rings=6, segments=8)
        add_ellipsoid(mesh, 'RiderWhite', WHITE, (-2.0, 0.0, 164.5), (13.5, 12.5, 4.0))
        _limb(mesh, 'RiderRed', RED,
              [(-2.0, 0.0, 165.0), (-6.0, 2.0, 176.0), (-14.0, 6.0, 184.0), (-24.0, 10.0, 184.0), (-30.0, 12.0, 176.0)],
              [11.5, 9.0, 6.0, 3.5, 2.0])
        add_ellipsoid(mesh, 'RiderWhite', WHITE, (-31.0, 12.5, 172.5), (4.5, 4.5, 4.5), rings=6, segments=8)
    else:
        add_ellipsoid(mesh, 'RiderBlack', BLACK, (-4.0, 0.0, 160.0), (11.8, 10.9, 10.5), rings=8, segments=14)  # hair
    return mesh


# ----------------------------------------------------------------------------------------------
# Jointed rider: the same figure as build_rider(), cut into parts that the game poses every frame
# (RiderRig.cpp). The torso's origin is the pelvis, facing +X with +Z up. Each limb part's origin
# is its upper joint, with the bone running along +X and +Z the way the joint bends (knee
# forwards, elbow down). RiderRig.cpp has the same lengths and joint positions: change them together.
# ----------------------------------------------------------------------------------------------

RIDER_THIGH_LENGTH = 45.0
RIDER_SHIN_LENGTH = 42.0
RIDER_UPPER_ARM_LENGTH = 26.5
RIDER_FOREARM_LENGTH = 27.0
RIDER_PELVIS = (-10.0, 0.0, 82.0)   # where the pelvis is in build_rider()'s coordinates


def build_rider_torso(variant):
    """Pelvis, trunk, chest, neck and head: everything that is not a limb. variant: 'santa' or 'wetsuit'."""
    santa = variant == 'santa'
    mesh = Mesh()
    body_material, body_color = ('RiderSkin', SKIN) if santa else ('RiderWetsuit', WETSUIT)
    ox, oy, oz = RIDER_PELVIS

    def at(point):
        return (point[0] - ox, point[1] - oy, point[2] - oz)

    if santa:
        add_ellipsoid(mesh, 'RiderRed', RED, at((-9.0, 0.0, 86.0)), (19.0, 24.0, 14.0))        # swim trunks
        add_ellipsoid(mesh, 'RiderWhite', WHITE, at((-8.0, 0.0, 96.0)), (20.5, 25.0, 3.5))     # waistband trim
        add_ellipsoid(mesh, 'RiderBlack', BLACK, at((-7.0, 0.0, 91.0)), (20.2, 25.2, 2.2))     # belt
        add_ellipsoid(mesh, 'RiderSkin', SKIN, at((-2.0, 0.0, 109.0)), (24.0, 25.0, 21.0))     # belly
    else:
        add_ellipsoid(mesh, body_material, body_color, at((-9.0, 0.0, 87.0)), (16.0, 20.0, 14.0))
        add_ellipsoid(mesh, body_material, body_color, at((-6.0, 0.0, 107.0)), (15.0, 19.0, 19.0))
        add_ellipsoid(mesh, 'RiderAccent', ACCENT, at((-6.0, 0.0, 99.0)), (15.6, 19.6, 3.0))   # harness
    add_ellipsoid(mesh, body_material, body_color, at((-6.0, 0.0, 129.0)), (17.0, 23.0, 15.0))  # chest
    _limb(mesh, 'RiderSkin', SKIN, [at((-5.0, 0.0, 138.0)), at((-3.0, 0.0, 148.0))], [6.0, 5.5])  # neck

    add_ellipsoid(mesh, 'RiderSkin', SKIN, at((-2.0, 0.0, 156.0)), (11.5, 10.5, 12.5), rings=10, segments=14)  # head
    add_ellipsoid(mesh, 'RiderSkin', SKIN, at((9.5, 0.0, 156.0)), (3.0, 2.6, 2.6), rings=6, segments=8)       # nose
    for side in (-1.0, 1.0):
        add_ellipsoid(mesh, 'RiderBlack', BLACK, at((8.0, side * 4.2, 160.0)), (1.6, 1.6, 1.8), rings=6, segments=8)

    if santa:
        add_ellipsoid(mesh, 'RiderWhite', WHITE, at((5.0, 0.0, 147.5)), (9.5, 11.0, 10.5))
        add_ellipsoid(mesh, 'RiderWhite', WHITE, at((2.0, 0.0, 140.0)), (8.0, 8.5, 9.0))
        for side in (-1.0, 1.0):
            add_ellipsoid(mesh, 'RiderWhite', WHITE, at((9.5, side * 4.0, 152.5)), (2.6, 4.2, 1.8), rings=6, segments=8)
        add_ellipsoid(mesh, 'RiderWhite', WHITE, at((-2.0, 0.0, 164.5)), (13.5, 12.5, 4.0))
        _limb(mesh, 'RiderRed', RED,
              [at((-2.0, 0.0, 165.0)), at((-6.0, 2.0, 176.0)), at((-14.0, 6.0, 184.0)), at((-24.0, 10.0, 184.0)), at((-30.0, 12.0, 176.0))],
              [11.5, 9.0, 6.0, 3.5, 2.0])
        add_ellipsoid(mesh, 'RiderWhite', WHITE, at((-31.0, 12.5, 172.5)), (4.5, 4.5, 4.5), rings=6, segments=8)
    else:
        add_ellipsoid(mesh, 'RiderBlack', BLACK, at((-4.0, 0.0, 160.0)), (11.8, 10.9, 10.5), rings=8, segments=14)  # hair
    return mesh


def build_rider_limb(variant, part):
    """part: 'thigh', 'shin', 'upper_arm' or 'forearm'. Bone along +X from the origin."""
    santa = variant == 'santa'
    mesh = Mesh()
    body_material, body_color = ('RiderSkin', SKIN) if santa else ('RiderWetsuit', WETSUIT)
    if part == 'thigh':
        length = RIDER_THIGH_LENGTH
        _limb(mesh, body_material, body_color, [(0.0, 0.0, 0.0), (length, 0.0, 0.0)], [10.5, 7.5])
        add_ellipsoid(mesh, body_material, body_color, (length, 0.0, 0.0), (7.8, 7.8, 7.8), rings=6, segments=10)  # knee
        if santa:
            add_ellipsoid(mesh, 'RiderRed', RED, (0.28 * length, 0.0, 0.0), (9.0, 11.5, 11.5), rings=6, segments=12)   # trouser leg
            add_ellipsoid(mesh, 'RiderWhite', WHITE, (0.55 * length, 0.0, 0.0), (3.2, 10.5, 10.5), rings=6, segments=12)  # hem
    elif part == 'shin':
        length = RIDER_SHIN_LENGTH
        _limb(mesh, body_material, body_color, [(0.0, 0.0, 0.0), (length, 0.0, 0.0)], [7.5, 5.5])
        # The foot, pointing the way the knee bends.
        add_ellipsoid(mesh, 'RiderSkin', SKIN, (length + 1.0, 0.0, 6.0), (4.5, 6.0, 13.0))
    elif part == 'upper_arm':
        length = RIDER_UPPER_ARM_LENGTH
        add_ellipsoid(mesh, body_material, body_color, (0.0, 0.0, 0.0), (7.5, 7.5, 7.5), rings=6, segments=10)  # shoulder
        _limb(mesh, body_material, body_color, [(0.0, 0.0, 0.0), (length, 0.0, 0.0)], [6.5, 5.5])
    elif part == 'forearm':
        length = RIDER_FOREARM_LENGTH
        add_ellipsoid(mesh, body_material, body_color, (0.0, 0.0, 0.0), (5.6, 5.6, 5.6), rings=6, segments=10)  # elbow
        _limb(mesh, body_material, body_color, [(0.0, 0.0, 0.0), (length, 0.0, 0.0)], [5.5, 4.5])
        add_ellipsoid(mesh, 'RiderSkin', SKIN, (length, 0.0, 0.0), (5.5, 5.0, 5.0), rings=6, segments=10)       # hand
    else:
        raise ValueError(part)
    return mesh


RIDER_PARTS = (('Torso', None), ('Thigh', 'thigh'), ('Shin', 'shin'), ('UpperArm', 'upper_arm'), ('Forearm', 'forearm'))


def generate_rider_parts(output_dir):
    """Writes the jointed riders' OBJs and returns {asset name: path}."""
    os.makedirs(output_dir, exist_ok=True)
    paths = {}
    for variant, label in (('santa', 'Santa'), ('wetsuit', 'Wetsuit')):
        for part_label, part in RIDER_PARTS:
            name = f'SM_Rider{label}_{part_label}'
            path = os.path.join(output_dir, f'rider_{variant}_{part_label.lower()}.obj')
            mesh = build_rider_torso(variant) if part is None else build_rider_limb(variant, part)
            mesh.write(path)
            paths[name] = path
    return paths


def generate_board_obj(filepath):
    # Twin-tip kiteboard
    # Dimensions: length = 140 cm, width = 42 cm, thickness = 2.5 cm, rocker = 4 cm
    length_half = 70.0
    width_half = 21.0
    thickness_half = 1.25
    rocker = 4.0
    x_segs = 14
    y_segs = 6
    
    vertices = []
    uvs = []
    normals = []
    faces = []
    
    # Top surface vertices
    top_indices = []
    for i in range(x_segs + 1):
        u = i / x_segs
        x = (u - 0.5) * 2.0 * length_half
        # Outline taper towards tips: narrower at ends
        taper = 1.0 - 0.25 * ((u - 0.5) * 2.0)**2
        current_width_half = width_half * taper
        # Continuous rocker curve
        z_rocker = rocker * ((u - 0.5) * 2.0)**2
        row = []
        for j in range(y_segs + 1):
            v = j / y_segs
            y = (v - 0.5) * 2.0 * current_width_half
            z = z_rocker + thickness_half
            vertices.append((x, y, z))
            uvs.append((u, v))
            normals.append((0.0, 0.0, 1.0))
            row.append(len(vertices))
        top_indices.append(row)
        
    # Bottom surface vertices
    bot_indices = []
    for i in range(x_segs + 1):
        u = i / x_segs
        x = (u - 0.5) * 2.0 * length_half
        taper = 1.0 - 0.25 * ((u - 0.5) * 2.0)**2
        current_width_half = width_half * taper
        z_rocker = rocker * ((u - 0.5) * 2.0)**2
        row = []
        for j in range(y_segs + 1):
            v = j / y_segs
            y = (v - 0.5) * 2.0 * current_width_half
            z = z_rocker - thickness_half
            vertices.append((x, y, z))
            uvs.append((u, v))
            normals.append((0.0, 0.0, -1.0))
            row.append(len(vertices))
        bot_indices.append(row)
        
    # Top quads
    for i in range(x_segs):
        for j in range(y_segs):
            idx0 = top_indices[i][j]
            idx1 = top_indices[i + 1][j]
            idx2 = top_indices[i + 1][j + 1]
            idx3 = top_indices[i][j + 1]
            faces.append((idx0, idx1, idx2))
            faces.append((idx0, idx2, idx3))
            
    # Bottom quads (reverse winding)
    for i in range(x_segs):
        for j in range(y_segs):
            idx0 = bot_indices[i][j]
            idx1 = bot_indices[i + 1][j]
            idx2 = bot_indices[i + 1][j + 1]
            idx3 = bot_indices[i][j + 1]
            faces.append((idx0, idx2, idx1))
            faces.append((idx0, idx3, idx2))
            
    # Side perimeter quads
    for i in range(x_segs):
        # -y edge
        t0 = top_indices[i][0]
        t1 = top_indices[i + 1][0]
        b0 = bot_indices[i][0]
        b1 = bot_indices[i + 1][0]
        faces.append((t0, b0, b1))
        faces.append((t0, b1, t1))
        # +y edge
        t0 = top_indices[i][y_segs]
        t1 = top_indices[i + 1][y_segs]
        b0 = bot_indices[i][y_segs]
        b1 = bot_indices[i + 1][y_segs]
        faces.append((t0, b1, b0))
        faces.append((t0, t1, b1))
        
    for j in range(y_segs):
        # -x tip
        t0 = top_indices[0][j]
        t1 = top_indices[0][j + 1]
        b0 = bot_indices[0][j]
        b1 = bot_indices[0][j + 1]
        faces.append((t0, b1, b0))
        faces.append((t0, t1, b1))
        # +x tip
        t0 = top_indices[x_segs][j]
        t1 = top_indices[x_segs][j + 1]
        b0 = bot_indices[x_segs][j]
        b1 = bot_indices[x_segs][j + 1]
        faces.append((t0, b0, b1))
        faces.append((t0, b1, t1))
        
    with open(filepath, 'w') as f:
        f.write('# Board Mesh\n')
        for v in vertices:
            f.write(f'v {v[0]:.4f} {v[1]:.4f} {v[2]:.4f}\n')
        for vt in uvs:
            f.write(f'vt {vt[0]:.4f} {vt[1]:.4f}\n')
        for vn in normals:
            f.write(f'vn {vn[0]:.4f} {vn[1]:.4f} {vn[2]:.4f}\n')
        for f_idx in faces:
            f.write(f'f {f_idx[0]}/{f_idx[0]}/{f_idx[0]} {f_idx[1]}/{f_idx[1]}/{f_idx[1]} {f_idx[2]}/{f_idx[2]}/{f_idx[2]}\n')

def generate_bar_obj(filepath):
    # Control bar: length = 55 cm, diameter = 3 cm
    # Cylinder oriented along Y axis
    length_half = 27.5
    radius = 1.5
    segments = 12
    y_segs = 4
    
    vertices = []
    uvs = []
    normals = []
    faces = []
    
    rows = []
    for i in range(y_segs + 1):
        u = i / y_segs
        y = (u - 0.5) * 2.0 * length_half
        row = []
        for j in range(segments):
            angle = 2.0 * math.pi * j / segments
            x = radius * math.cos(angle)
            z = radius * math.sin(angle)
            vertices.append((x, y, z))
            uvs.append((u, j / segments))
            normals.append((math.cos(angle), 0.0, math.sin(angle)))
            row.append(len(vertices))
        rows.append(row)
        
    for i in range(y_segs):
        for j in range(segments):
            next_j = (j + 1) % segments
            idx0 = rows[i][j]
            idx1 = rows[i + 1][j]
            idx2 = rows[i + 1][next_j]
            idx3 = rows[i][next_j]
            faces.append((idx0, idx1, idx2))
            faces.append((idx0, idx2, idx3))
            
    with open(filepath, 'w') as f:
        f.write('# Bar Mesh\n')
        for v in vertices:
            f.write(f'v {v[0]:.4f} {v[1]:.4f} {v[2]:.4f}\n')
        for vt in uvs:
            f.write(f'vt {vt[0]:.4f} {vt[1]:.4f}\n')
        for vn in normals:
            f.write(f'vn {vn[0]:.4f} {vn[1]:.4f} {vn[2]:.4f}\n')
        for f_idx in faces:
            f.write(f'f {f_idx[0]}/{f_idx[0]}/{f_idx[0]} {f_idx[1]}/{f_idx[1]}/{f_idx[1]} {f_idx[2]}/{f_idx[2]}/{f_idx[2]}\n')


# ----------------------------------------------------------------------------------------------
# Spot scenery: an island with palms, a sandbar, and a shark. Origins are at the water surface.
# The island and the sandbar are the tops of flattened ellipsoids sunk a little below the water;
# KiteSurfSpot.cpp has the same radii and depths, to know where the sand is. Change them together.
# ----------------------------------------------------------------------------------------------

SAND = (0.86, 0.78, 0.56)
PALM_TRUNK = (0.42, 0.30, 0.18)
PALM_LEAF = (0.13, 0.45, 0.16)
SHARK = (0.36, 0.42, 0.47)

ISLAND_RADII = (2000.0, 1500.0, 350.0)
ISLAND_CENTRE_Z = -80.0
SANDBAR_RADII = (3000.0, 400.0, 90.0)
SANDBAR_CENTRE_Z = -40.0


def _sand_height(radii, centre_z, x, y):
    inside = 1.0 - (x / radii[0]) ** 2 - (y / radii[1]) ** 2
    return centre_z + radii[2] * math.sqrt(max(inside, 0.0))


def _add_palm(mesh, base, lean, height):
    """A palm with a curved trunk leaning the way of `lean` (x, y) and a crown of drooping fronds."""
    trunk = []
    for i in range(7):
        t = i / 6.0
        trunk.append((base[0] + lean[0] * t * t, base[1] + lean[1] * t * t, base[2] + height * t))
    add_tube(mesh, 'PalmTrunk', PALM_TRUNK, trunk, [20.0 - 9.0 * i / 6.0 for i in range(7)], segments=8)
    top = trunk[-1]
    fronds = 8
    for f in range(fronds):
        angle = 2.0 * math.pi * f / fronds + 0.3
        path = []
        for i in range(6):
            t = i / 5.0
            reach = 300.0 * t
            droop = 60.0 * t - 190.0 * t * t
            path.append((top[0] + math.cos(angle) * reach, top[1] + math.sin(angle) * reach, top[2] + droop))
        add_tube(mesh, 'PalmLeaf', PALM_LEAF, path, [16.0, 26.0, 28.0, 22.0, 13.0, 3.0], segments=5)
    add_ellipsoid(mesh, 'PalmLeaf', PALM_LEAF, top, (26.0, 26.0, 20.0), rings=5, segments=8)


def build_island():
    mesh = Mesh()
    add_ellipsoid(mesh, 'Sand', SAND, (0.0, 0.0, ISLAND_CENTRE_Z), ISLAND_RADII, rings=14, segments=28)
    for (x, y), lean, height in (((250.0, -150.0), (130.0, -40.0), 720.0),
                                 ((-420.0, 260.0), (-150.0, 90.0), 640.0),
                                 ((-80.0, -380.0), (40.0, -170.0), 560.0)):
        base_z = _sand_height(ISLAND_RADII, ISLAND_CENTRE_Z, x, y) - 15.0
        _add_palm(mesh, (x, y, base_z), lean, height)
    return mesh


def build_sandbar():
    mesh = Mesh()
    add_ellipsoid(mesh, 'Sand', SAND, (0.0, 0.0, SANDBAR_CENTRE_Z), SANDBAR_RADII, rings=10, segments=28)
    return mesh


def build_shark():
    """Swims along +X just under the surface with its dorsal fin out of the water."""
    mesh = Mesh()
    add_ellipsoid(mesh, 'Shark', SHARK, (0.0, 0.0, -55.0), (170.0, 36.0, 40.0), rings=8, segments=12)
    dorsal = [(45.0, 0.0, -22.0), (-55.0, 0.0, -22.0), (-40.0, 0.0, 58.0)]
    mesh.add_part('Shark', SHARK, dorsal, [(0.0, 0.0), (1.0, 0.0), (0.5, 1.0)], [(0, 1, 2)], double_sided=True)
    tail = [(-150.0, 0.0, -55.0), (-235.0, 0.0, 5.0), (-225.0, 0.0, -105.0)]
    mesh.add_part('Shark', SHARK, tail, [(0.0, 0.5), (1.0, 1.0), (1.0, 0.0)], [(0, 1, 2)], double_sided=True)
    for side in (-1.0, 1.0):
        fin = [(60.0, side * 30.0, -65.0), (10.0, side * 30.0, -65.0), (5.0, side * 95.0, -85.0)]
        mesh.add_part('Shark', SHARK, fin, [(0.0, 0.0), (1.0, 0.0), (0.5, 1.0)], [(0, 1, 2)], double_sided=True)
    return mesh


def generate_spot(output_dir):
    """Writes the scenery OBJs and returns {asset name: path}."""
    os.makedirs(output_dir, exist_ok=True)
    paths = {
        'SM_Island': os.path.join(output_dir, 'island.obj'),
        'SM_Sandbar': os.path.join(output_dir, 'sandbar.obj'),
        'SM_Shark': os.path.join(output_dir, 'shark.obj'),
    }
    build_island().write(paths['SM_Island'])
    build_sandbar().write(paths['SM_Sandbar'])
    build_shark().write(paths['SM_Shark'])
    return paths


def build_preview_backdrop():
    """A 1 m square card facing +X: the sky behind the gear preview, scaled in C++. The importer flips V,
    so v = 1 here is the top of the card, v = 0 in the material."""
    mesh = Mesh()
    positions = [(0.0, -50.0, 50.0), (0.0, 50.0, 50.0), (0.0, 50.0, -50.0), (0.0, -50.0, -50.0)]
    uvs = [(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)]
    mesh.add_part('PreviewBackdrop', (0.5, 0.75, 0.9), positions, uvs, [(0, 2, 1), (0, 3, 2)], double_sided=True)
    return mesh


def generate_preview(output_dir):
    """Writes the gear preview's OBJs and returns {asset name: path}."""
    os.makedirs(output_dir, exist_ok=True)
    paths = {'SM_PreviewBackdrop': os.path.join(output_dir, 'preview_backdrop.obj')}
    build_preview_backdrop().write(paths['SM_PreviewBackdrop'])
    return paths


def generate_all(output_dir):
    """Writes every OBJ and returns {asset name: path}."""
    os.makedirs(output_dir, exist_ok=True)
    paths = {
        'SM_Kite': os.path.join(output_dir, 'kite.obj'),
        'SM_KiteBoost': os.path.join(output_dir, 'kite_boost.obj'),
        'SM_KiteBoard': os.path.join(output_dir, 'board.obj'),
        'SM_ControlBar': os.path.join(output_dir, 'control_bar.obj'),
        'SM_RiderSanta': os.path.join(output_dir, 'rider_santa.obj'),
        'SM_RiderWetsuit': os.path.join(output_dir, 'rider_wetsuit.obj'),
    }
    build_kite().write(paths['SM_Kite'])
    build_kite(KITE_BOOST_STRUT_POSITIONS, 'KiteCanopyBoost').write(paths['SM_KiteBoost'])
    generate_board_obj(paths['SM_KiteBoard'])
    generate_bar_obj(paths['SM_ControlBar'])
    build_rider('santa').write(paths['SM_RiderSanta'])
    build_rider('wetsuit').write(paths['SM_RiderWetsuit'])
    return paths


if __name__ == '__main__':
    import sys
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'Geometry')
    for name, path in {**generate_all(out), **generate_spot(out), **generate_preview(out), **generate_rider_parts(out)}.items():
        print(f'{name}: {path}')
