import math

def generate_kite_obj(filepath):
    # Generate an aerodynamic curved foil kite (leading edge tube + canopy)
    # Kite dimensions: span = 400 cm (4 m), chord = 120 cm (1.2 m), arc sag = 80 cm
    # Centered at (0, 0, 0)
    span_segments = 16
    chord_segments = 8
    span_half = 200.0
    chord_len = 120.0
    arc_depth = 90.0
    
    vertices = []
    uvs = []
    normals = []
    faces = []
    
    for i in range(span_segments + 1):
        u = i / span_segments
        # span coordinate: -span_half to +span_half
        y = (u - 0.5) * 2.0 * span_half
        # Arc curve (C-shape or bow kite curve)
        # At center (u=0.5): z = 0, at tips: z = -arc_depth
        theta = (u - 0.5) * math.pi
        z_arc = -arc_depth * (1.0 - math.cos(theta))
        # Slight sweep back at tips
        x_sweep = -30.0 * (1.0 - math.cos(theta))
        
        for j in range(chord_segments + 1):
            v = j / chord_segments
            # chord from leading edge (x=0) to trailing edge (x=-chord_len)
            # Profile thickness / camber
            camber = 15.0 * math.sin(v * math.pi) * (1.0 - 0.4 * abs(u - 0.5) * 2.0)
            x = x_sweep - v * chord_len
            z = z_arc + camber
            
            vertices.append((x, y, z))
            uvs.append((u, v))
            # Approximate normal pointing upwards/outwards
            normals.append((0.0, 0.0, 1.0))
            
    for i in range(span_segments):
        for j in range(chord_segments):
            # 4 vertices of quad
            idx0 = i * (chord_segments + 1) + j + 1
            idx1 = (i + 1) * (chord_segments + 1) + j + 1
            idx2 = (i + 1) * (chord_segments + 1) + (j + 1) + 1
            idx3 = i * (chord_segments + 1) + (j + 1) + 1
            # Two triangles (both sides visible if double sided, or upper surface)
            faces.append((idx0, idx1, idx2))
            faces.append((idx0, idx2, idx3))
            # Back faces
            faces.append((idx0, idx2, idx1))
            faces.append((idx0, idx3, idx2))
            
    with open(filepath, 'w') as f:
        f.write('# Kite Mesh\n')
        for v in vertices:
            f.write(f'v {v[0]:.4f} {v[1]:.4f} {v[2]:.4f}\n')
        for vt in uvs:
            f.write(f'vt {vt[0]:.4f} {vt[1]:.4f}\n')
        for vn in normals:
            f.write(f'vn {vn[0]:.4f} {vn[1]:.4f} {vn[2]:.4f}\n')
        for f_idx in faces:
            f.write(f'f {f_idx[0]}/{f_idx[0]}/{f_idx[0]} {f_idx[1]}/{f_idx[1]}/{f_idx[1]} {f_idx[2]}/{f_idx[2]}/{f_idx[2]}\n')

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

if __name__ == '__main__':
    import os
    os.makedirs('/home/koorik/.hermes/kanban/boards/demo/workspaces/t_96052fbe/kitesurf/scripts/editor/geometry', exist_ok=True)
    generate_kite_obj('/home/koorik/.hermes/kanban/boards/demo/workspaces/t_96052fbe/kitesurf/scripts/editor/geometry/kite.obj')
    generate_board_obj('/home/koorik/.hermes/kanban/boards/demo/workspaces/t_96052fbe/kitesurf/scripts/editor/geometry/board.obj')
    generate_bar_obj('/home/koorik/.hermes/kanban/boards/demo/workspaces/t_96052fbe/kitesurf/scripts/editor/geometry/control_bar.obj')
    print('Generated OBJ files successfully!')
