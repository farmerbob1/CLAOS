#!/usr/bin/env python3
"""
BSP Builder for CLAOS 3D Engine
Compiles a JSON map file into a binary .bsp file.

Usage: python tools/bspbuild.py maps/demo.json -o maps/demo.bsp
"""

import argparse
import json
import math
import struct
import sys
from dataclasses import dataclass
from typing import List, Tuple

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
MAGIC = b"BSP!"
FIXED_SHIFT = 16
SUBSECTOR_FLAG = 0x8000
NO_SECTOR = 0xFFFF
MAX_LEAF_SEGS = 3  # create a subsector leaf when seg count <= this

# ---------------------------------------------------------------------------
# Data structures
# ---------------------------------------------------------------------------
@dataclass
class Vertex:
    x: float
    y: float

@dataclass
class Linedef:
    v1: int
    v2: int
    front_sector: int
    back_sector: int  # 0xFFFF = solid
    front_upper_tex: int = 0
    front_mid_tex: int = 0
    front_lower_tex: int = 0
    back_upper_tex: int = 0
    back_mid_tex: int = 0
    back_lower_tex: int = 0
    tex_offset_x: int = 0
    tex_offset_y: int = 0
    flags: int = 0

@dataclass
class Sector:
    floor_height: float
    ceiling_height: float
    floor_tex: int = 0
    ceiling_tex: int = 0
    light_level: int = 255
    flags: int = 0

@dataclass
class Seg:
    v1: int
    v2: int
    linedef: int
    side: int  # 0 = front, 1 = back
    offset: float = 0.0
    # Cached for partitioning
    sector: int = 0

@dataclass
class Subsector:
    first_seg: int
    num_segs: int
    sector: int

@dataclass
class Node:
    x: float
    y: float
    dx: float
    dy: float
    bbox_right: Tuple[int, int, int, int] = (0, 0, 0, 0)  # top, bottom, left, right
    bbox_left: Tuple[int, int, int, int] = (0, 0, 0, 0)
    child_right: int = 0
    child_left: int = 0

# ---------------------------------------------------------------------------
# Fixed-point conversion
# ---------------------------------------------------------------------------
def to_fixed(val: float) -> int:
    """Convert float to 16.16 fixed-point as a signed 32-bit integer."""
    return int(val * (1 << FIXED_SHIFT))

def to_fixed_signed(val: float) -> int:
    """Convert float to 16.16 fixed-point, clamp to signed 32-bit range."""
    v = int(val * (1 << FIXED_SHIFT))
    # Clamp to int32 range
    if v > 0x7FFFFFFF:
        v = 0x7FFFFFFF
    elif v < -0x80000000:
        v = -0x80000000
    return v

# ---------------------------------------------------------------------------
# Geometry helpers
# ---------------------------------------------------------------------------
EPSILON = 0.01  # half a map unit — safe for integer coords, handles split vertex imprecision

def cross2d(dx1, dy1, dx2, dy2):
    """2D cross product."""
    return dx1 * dy2 - dy1 * dx2

def seg_length(v1: Vertex, v2: Vertex) -> float:
    dx = v2.x - v1.x
    dy = v2.y - v1.y
    return math.sqrt(dx * dx + dy * dy)

def point_side(px, py, lx, ly, ldx, ldy):
    """Returns >0 for right, <0 for left, ~0 for on the line."""
    return cross2d(px - lx, py - ly, ldx, ldy)

def classify_seg(seg: 'Seg', vertices: List[Vertex], part_x, part_y, part_dx, part_dy):
    """
    Classify a seg relative to a partition line.
    Returns: 'right', 'left', 'on', or 'spanning'
    """
    v1 = vertices[seg.v1]
    v2 = vertices[seg.v2]
    d1 = point_side(v1.x, v1.y, part_x, part_y, part_dx, part_dy)
    d2 = point_side(v2.x, v2.y, part_x, part_y, part_dx, part_dy)

    if d1 > EPSILON and d2 > EPSILON:
        return 'right', d1, d2
    elif d1 < -EPSILON and d2 < -EPSILON:
        return 'left', d1, d2
    elif abs(d1) <= EPSILON and abs(d2) <= EPSILON:
        return 'on', d1, d2
    elif (d1 > EPSILON and d2 < -EPSILON) or (d1 < -EPSILON and d2 > EPSILON):
        return 'spanning', d1, d2
    else:
        # One endpoint on the line — treat as the side of the other endpoint
        if abs(d1) <= EPSILON:
            return ('right' if d2 > 0 else 'left'), d1, d2
        else:
            return ('right' if d1 > 0 else 'left'), d1, d2

def split_seg(seg: 'Seg', vertices: List[Vertex], part_x, part_y, part_dx, part_dy, d1, d2):
    """
    Split a seg at the partition line intersection.
    Returns (right_seg, left_seg) and may add a new vertex.
    """
    v1 = vertices[seg.v1]
    v2 = vertices[seg.v2]

    # Compute intersection parameter
    frac = d1 / (d1 - d2)
    frac = max(0.0, min(1.0, frac))

    # New vertex at split point (deduplicate to avoid near-identical verts)
    nx = v1.x + frac * (v2.x - v1.x)
    ny = v1.y + frac * (v2.y - v1.y)
    new_idx = None
    for i, v in enumerate(vertices):
        if abs(v.x - nx) < 0.01 and abs(v.y - ny) < 0.01:
            new_idx = i
            break
    if new_idx is None:
        new_idx = len(vertices)
        vertices.append(Vertex(nx, ny))

    # Offset for the new sub-segs
    orig_offset = seg.offset
    split_offset = orig_offset + frac * seg_length(v1, v2)

    seg_a = Seg(v1=seg.v1, v2=new_idx, linedef=seg.linedef, side=seg.side,
                offset=orig_offset, sector=seg.sector)
    seg_b = Seg(v1=new_idx, v2=seg.v2, linedef=seg.linedef, side=seg.side,
                offset=split_offset, sector=seg.sector)

    # d1 > 0 means v1 is on right side
    if d1 > 0:
        return seg_a, seg_b  # right, left
    else:
        return seg_b, seg_a  # right, left

def compute_bbox(segs: List['Seg'], vertices: List[Vertex]):
    """Compute bounding box (top, bottom, left, right) for a set of segs."""
    min_x = float('inf')
    max_x = float('-inf')
    min_y = float('inf')
    max_y = float('-inf')
    for s in segs:
        for vi in (s.v1, s.v2):
            v = vertices[vi]
            min_x = min(min_x, v.x)
            max_x = max(max_x, v.x)
            min_y = min(min_y, v.y)
            max_y = max(max_y, v.y)
    # top, bottom, left, right — top = max_y, bottom = min_y
    return (int(max_y), int(min_y), int(min_x), int(max_x))

# ---------------------------------------------------------------------------
# BSP Builder
# ---------------------------------------------------------------------------
class BSPBuilder:
    def __init__(self, vertices, linedefs, sectors):
        self.vertices: List[Vertex] = vertices
        self.linedefs: List[Linedef] = linedefs
        self.sectors: List[Sector] = sectors
        self.segs: List[Seg] = []
        self.subsectors: List[Subsector] = []
        self.nodes: List[Node] = []
        self.final_segs: List[Seg] = []  # segs in subsector order

    def build(self):
        """Run the full BSP build pipeline."""
        self._generate_segs()
        if not self.segs:
            print("Warning: no segs generated", file=sys.stderr)
            return 0
        root = self._build_node(list(range(len(self.segs))))
        return root

    def _generate_segs(self):
        """Generate segs from linedefs."""
        for i, ld in enumerate(self.linedefs):
            v1 = self.vertices[ld.v1]
            v2 = self.vertices[ld.v2]
            length = seg_length(v1, v2)

            # Front seg
            front_seg = Seg(
                v1=ld.v1, v2=ld.v2, linedef=i, side=0,
                offset=0.0, sector=ld.front_sector
            )
            self.segs.append(front_seg)

            # Back seg (only for two-sided lines)
            if ld.back_sector != NO_SECTOR:
                back_seg = Seg(
                    v1=ld.v2, v2=ld.v1, linedef=i, side=1,
                    offset=0.0, sector=ld.back_sector
                )
                self.segs.append(back_seg)

    def _pick_partition(self, seg_indices: List[int]) -> int:
        """Pick the best partition seg minimizing splits + imbalance."""
        best_score = float('inf')
        best_idx = seg_indices[0]

        for candidate_i in seg_indices:
            cand = self.segs[candidate_i]
            cv1 = self.vertices[cand.v1]
            cv2 = self.vertices[cand.v2]
            part_x, part_y = cv1.x, cv1.y
            part_dx = cv2.x - cv1.x
            part_dy = cv2.y - cv1.y

            left_count = 0
            right_count = 0
            split_count = 0

            for si in seg_indices:
                if si == candidate_i:
                    right_count += 1
                    continue
                seg = self.segs[si]
                side, d1, d2 = classify_seg(seg, self.vertices, part_x, part_y, part_dx, part_dy)
                if side == 'right' or side == 'on':
                    right_count += 1
                elif side == 'left':
                    left_count += 1
                elif side == 'spanning':
                    split_count += 1

            imbalance = abs(left_count - right_count)
            score = split_count * 5 + imbalance
            if score < best_score:
                best_score = score
                best_idx = candidate_i

        return best_idx

    def _is_convex(self, seg_indices: List[int]) -> bool:
        """Check if a set of segs forms a convex subsector."""
        if len(seg_indices) <= MAX_LEAF_SEGS:
            return True

        # Check if all segs are on the same side of every other seg
        for candidate_i in seg_indices:
            cand = self.segs[candidate_i]
            cv1 = self.vertices[cand.v1]
            cv2 = self.vertices[cand.v2]
            part_x, part_y = cv1.x, cv1.y
            part_dx = cv2.x - cv1.x
            part_dy = cv2.y - cv1.y

            has_left = False
            has_right = False
            for si in seg_indices:
                if si == candidate_i:
                    continue
                side, _, _ = classify_seg(self.segs[si], self.vertices,
                                          part_x, part_y, part_dx, part_dy)
                if side == 'left':
                    has_left = True
                elif side == 'right':
                    has_right = True
                if has_left and has_right:
                    return False
        return True

    def _make_subsector(self, seg_indices: List[int]) -> int:
        """Create a subsector leaf from a set of seg indices."""
        first = len(self.final_segs)
        # Determine sector from the first seg
        sector = self.segs[seg_indices[0]].sector if seg_indices else 0
        for si in seg_indices:
            self.final_segs.append(self.segs[si])
        ss = Subsector(first_seg=first, num_segs=len(seg_indices), sector=sector)
        idx = len(self.subsectors)
        self.subsectors.append(ss)
        return idx | SUBSECTOR_FLAG

    def _build_node(self, seg_indices: List[int]) -> int:
        """Recursively build BSP nodes. Returns a child index (with subsector flag if leaf)."""
        if not seg_indices:
            return self._make_subsector([])

        if self._is_convex(seg_indices):
            return self._make_subsector(seg_indices)

        # Pick partition line
        part_i = self._pick_partition(seg_indices)
        part_seg = self.segs[part_i]
        pv1 = self.vertices[part_seg.v1]
        pv2 = self.vertices[part_seg.v2]
        part_x, part_y = pv1.x, pv1.y
        part_dx = pv2.x - pv1.x
        part_dy = pv2.y - pv1.y

        right_segs = []
        left_segs = []

        for si in seg_indices:
            if si == part_i:
                right_segs.append(si)
                continue

            seg = self.segs[si]
            side, d1, d2 = classify_seg(seg, self.vertices, part_x, part_y, part_dx, part_dy)

            if side == 'right' or side == 'on':
                right_segs.append(si)
            elif side == 'left':
                left_segs.append(si)
            elif side == 'spanning':
                # Split the seg
                right_part, left_part = split_seg(seg, self.vertices,
                                                   part_x, part_y, part_dx, part_dy,
                                                   d1, d2)
                # Add new segs
                ri = len(self.segs)
                self.segs.append(right_part)
                li = len(self.segs)
                self.segs.append(left_part)
                right_segs.append(ri)
                left_segs.append(li)

        # Degenerate case: if one side is empty, make a leaf
        if not right_segs:
            return self._make_subsector(left_segs)
        if not left_segs:
            return self._make_subsector(right_segs)

        # Recurse
        child_right = self._build_node(right_segs)
        child_left = self._build_node(left_segs)

        # Compute bounding boxes
        right_seg_objs = [self.segs[i] if i < len(self.segs) else self.final_segs[0]
                          for i in right_segs]
        left_seg_objs = [self.segs[i] if i < len(self.segs) else self.final_segs[0]
                         for i in left_segs]
        bbox_right = compute_bbox(right_seg_objs, self.vertices)
        bbox_left = compute_bbox(left_seg_objs, self.vertices)

        node = Node(
            x=part_x, y=part_y,
            dx=part_dx, dy=part_dy,
            bbox_right=bbox_right,
            bbox_left=bbox_left,
            child_right=child_right,
            child_left=child_left
        )
        idx = len(self.nodes)
        self.nodes.append(node)
        return idx

# ---------------------------------------------------------------------------
# Binary writer
# ---------------------------------------------------------------------------
def write_bsp(filename: str, builder: BSPBuilder, root_node: int):
    """Write the compiled BSP data to a binary file."""
    with open(filename, 'wb') as f:
        num_verts = len(builder.vertices)
        num_linedefs = len(builder.linedefs)
        num_sectors = len(builder.sectors)
        num_segs = len(builder.final_segs)
        num_subsectors = len(builder.subsectors)
        num_nodes = len(builder.nodes)

        # Resolve root_node: if it has subsector flag, keep it as-is
        root = root_node & 0xFFFF

        # Header (20 bytes): magic(4) + 8 x uint16(16)
        f.write(MAGIC)
        f.write(struct.pack('<HHHHHHHH',
            num_verts, num_linedefs, num_sectors,
            num_segs, num_subsectors, num_nodes,
            root, 0))

        # Vertices (8 bytes each)
        for v in builder.vertices:
            f.write(struct.pack('<ii', to_fixed_signed(v.x), to_fixed_signed(v.y)))

        # Linedefs (36 bytes each)
        for ld in builder.linedefs:
            f.write(struct.pack('<HH HH HHH HHH ii HH',
                ld.v1, ld.v2,
                ld.front_sector, ld.back_sector,
                ld.front_upper_tex, ld.front_mid_tex, ld.front_lower_tex,
                ld.back_upper_tex, ld.back_mid_tex, ld.back_lower_tex,
                to_fixed_signed(ld.tex_offset_x), to_fixed_signed(ld.tex_offset_y),
                ld.flags, 0))

        # Sectors (16 bytes each)
        for sec in builder.sectors:
            f.write(struct.pack('<ii HH B BBB HH',
                to_fixed_signed(sec.floor_height),
                to_fixed_signed(sec.ceiling_height),
                sec.floor_tex, sec.ceiling_tex,
                sec.light_level,
                0, 0, 0,  # pad[3]
                sec.flags, 0))

        # Segs (12 bytes each)
        for seg in builder.final_segs:
            f.write(struct.pack('<HHHH i',
                seg.v1, seg.v2, seg.linedef, seg.side,
                to_fixed_signed(seg.offset)))

        # Subsectors (8 bytes each)
        for ss in builder.subsectors:
            f.write(struct.pack('<HHHH',
                ss.first_seg, ss.num_segs, ss.sector, 0))

        # Nodes (32 bytes each)
        for node in builder.nodes:
            f.write(struct.pack('<iiii',
                to_fixed_signed(node.x), to_fixed_signed(node.y),
                to_fixed_signed(node.dx), to_fixed_signed(node.dy)))
            # bbox_right: top, bottom, left, right as int16
            f.write(struct.pack('<hhhh', *node.bbox_right))
            # bbox_left: top, bottom, left, right as int16
            f.write(struct.pack('<hhhh', *node.bbox_left))
            f.write(struct.pack('<HH',
                node.child_right & 0xFFFF,
                node.child_left & 0xFFFF))

# ---------------------------------------------------------------------------
# JSON loader
# ---------------------------------------------------------------------------
def load_map(filename: str):
    """Load a JSON map file and return BSPBuilder-ready data."""
    with open(filename, 'r') as f:
        data = json.load(f)

    vertices = [Vertex(v[0], v[1]) for v in data['vertices']]

    sectors = []
    for s in data['sectors']:
        sectors.append(Sector(
            floor_height=s.get('floor', 0),
            ceiling_height=s.get('ceiling', 128),
            floor_tex=s.get('floor_tex', 0),
            ceiling_tex=s.get('ceil_tex', 0),
            light_level=s.get('light', 255),
            flags=s.get('flags', 0)
        ))

    linedefs = []
    for ld in data['linedefs']:
        back = ld.get('back', -1)
        linedefs.append(Linedef(
            v1=ld['v1'],
            v2=ld['v2'],
            front_sector=ld['front'],
            back_sector=NO_SECTOR if back == -1 else back,
            front_upper_tex=ld.get('upper_tex', 0),
            front_mid_tex=ld.get('mid_tex', 0),
            front_lower_tex=ld.get('lower_tex', 0),
            back_upper_tex=ld.get('back_upper_tex', 0),
            back_mid_tex=ld.get('back_mid_tex', 0),
            back_lower_tex=ld.get('back_lower_tex', 0),
            tex_offset_x=ld.get('tex_offset_x', 0),
            tex_offset_y=ld.get('tex_offset_y', 0),
            flags=ld.get('flags', 0)
        ))

    return vertices, linedefs, sectors

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description='CLAOS BSP Builder — compile JSON map to binary .bsp')
    parser.add_argument('input', help='Input JSON map file')
    parser.add_argument('-o', '--output', default=None, help='Output .bsp file (default: input with .bsp extension)')
    parser.add_argument('-v', '--verbose', action='store_true', help='Print build statistics')
    args = parser.parse_args()

    output = args.output
    if output is None:
        output = args.input.rsplit('.', 1)[0] + '.bsp'

    print(f"BSP Builder: {args.input} -> {output}")

    vertices, linedefs, sectors = load_map(args.input)
    print(f"  Loaded: {len(vertices)} vertices, {len(linedefs)} linedefs, {len(sectors)} sectors")

    builder = BSPBuilder(vertices, linedefs, sectors)
    root_node = builder.build()

    print(f"  Built:  {len(builder.final_segs)} segs, {len(builder.subsectors)} subsectors, {len(builder.nodes)} nodes")
    print(f"  Vertices after splits: {len(builder.vertices)}")

    write_bsp(output, builder, root_node)

    # Verify output size
    import os
    size = os.path.getsize(output)
    expected = (20  # header: magic(4) + 8*uint16(16)
                + len(builder.vertices) * 8     # vertex: 2 x int32
                + len(builder.linedefs) * 32    # linedef: 10*uint16 + 2*int32 + 2*uint16
                + len(builder.sectors) * 20     # sector: 2*int32 + 2*uint16 + 1+3+2+2
                + len(builder.final_segs) * 12  # seg: 4*uint16 + int32
                + len(builder.subsectors) * 8   # subsector: 4*uint16
                + len(builder.nodes) * 36)      # node: 4*int32(16) + 8*int16(16) + 2*uint16(4)
    if size != expected:
        print(f"  WARNING: file size {size} != expected {expected}", file=sys.stderr)
    else:
        print(f"  Output: {size} bytes OK")

    if args.verbose:
        print(f"\n  Root node: {root_node} (subsector={bool(root_node & SUBSECTOR_FLAG)})")
        for i, n in enumerate(builder.nodes):
            print(f"  Node {i}: partition=({n.x},{n.y})+({n.dx},{n.dy}) "
                  f"R={n.child_right:#06x} L={n.child_left:#06x}")
        for i, ss in enumerate(builder.subsectors):
            print(f"  Subsector {i}: segs[{ss.first_seg}..{ss.first_seg+ss.num_segs-1}] sector={ss.sector}")

if __name__ == '__main__':
    main()
