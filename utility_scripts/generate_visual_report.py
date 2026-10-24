import argparse
import html
import json
import math
import os
import sys

from analyze_body_samples import convex_hull_2d, slice_triangles_at_z
from nif_loader import load_nif_mesh
from test_body_logic import (
    BASE_3BA_NIF,
    BASE_HIMBO_NIF,
    DEFAULT_DLL_PATH,
    GROUND_TRUTH_JSON,
    REPO_ROOT,
    BodyLanguageCppBridge,
)


def extract_slice_polygons_at_z(
    shape: dict,
    z_norm: float,
    max_abs_x: float = 17.5,
    min_x: float = -17.5,
    excl_breast: bool = False,
    max_breast_w: float = 0.05,
    excl_thigh: bool = False,
    max_thigh_w: float = 0.35,
    allow_calf: bool = False,
    max_front_y: float = None,
):
    """Extracts both the 72-sector true contour polygon and the convex-hull tape polygon at z_norm."""
    verts = shape["verts"]
    tris = shape["tris"]
    limb_w = shape["limb_w"]
    breast_w = shape["breast_w"]
    thigh_w = shape["thigh_w"]
    z_plane = shape["min_z"] + z_norm * shape["h0"]

    intersect_pts = []
    for i0, i1, i2 in tris:
        v0, v1, v2 = verts[i0], verts[i1], verts[i2]
        z0, z1, z2 = v0[2], v1[2], v2[2]
        if (z0 < z_plane and z1 < z_plane and z2 < z_plane) or (
            z0 > z_plane and z1 > z_plane and z2 > z_plane
        ):
            continue
        if not allow_calf and (limb_w[i0] > 0.15 or limb_w[i1] > 0.15 or limb_w[i2] > 0.15):
            continue
        if excl_breast and (
            breast_w[i0] > max_breast_w
            or breast_w[i1] > max_breast_w
            or breast_w[i2] > max_breast_w
        ):
            continue
        if excl_thigh and (
            thigh_w[i0] > max_thigh_w
            or thigh_w[i1] > max_thigh_w
            or thigh_w[i2] > max_thigh_w
        ):
            continue

        for a, b in ((v0, v1), (v1, v2), (v2, v0)):
            za, zb = a[2], b[2]
            if (za <= z_plane <= zb) or (zb <= z_plane <= za):
                dz = zb - za
                t = 0.5 if abs(dz) < 1e-7 else (z_plane - za) / dz
                ix = a[0] + t * (b[0] - a[0])
                iy = a[1] + t * (b[1] - a[1])
                if max_front_y is not None and iy > max_front_y:
                    iy = max_front_y
                if min_x <= ix <= max_abs_x:
                    intersect_pts.append((ix, iy))

    if len(intersect_pts) < 12:
        return [], []

    tape_hull = convex_hull_2d(intersect_pts)
    cx = sum(p[0] for p in intersect_pts) / len(intersect_pts)
    cy = sum(p[1] for p in intersect_pts) / len(intersect_pts)
    sector_pts = []
    for s in range(72):
        a0 = (s * 5.0 - 180.0) * math.pi / 180.0
        a1 = ((s + 1) * 5.0 - 180.0) * math.pi / 180.0
        sec = [p for p in intersect_pts if a0 <= math.atan2(p[1] - cy, p[0] - cx) < a1]
        if sec:
            sector_pts.append(max(sec, key=lambda p: (p[0] - cx) ** 2 + (p[1] - cy) ** 2))
    return sector_pts, tape_hull


def extract_arm_cross_section(shape: dict):
    """Extracts the perpendicular cross-section of the left upper arm (Ring 9)."""
    verts = shape["verts"]
    min_z = shape["min_z"]
    h0 = shape["h0"]
    upperarm_w = shape["upperarm_w"]
    forearm_w = shape["forearm_w"]
    inv_sqrt2 = 0.70710678

    arm_pts = [
        (x, y, z)
        for i, (x, y, z) in enumerate(verts)
        if 0.76 <= (z - min_z) / h0 <= 0.86
        and x > 11.5
        and upperarm_w[i] >= 0.25
        and forearm_w[i] <= 0.15
    ]
    if len(arm_pts) < 8:
        return [], (16.0, min_z + 0.81 * h0)

    ax = sum(p[0] for p in arm_pts) / len(arm_pts)
    ay = sum(p[1] for p in arm_pts) / len(arm_pts)
    az = sum(p[2] for p in arm_pts) / len(arm_pts)
    band = [
        (p[1] - ay - 9.0, (p[2] - az + (p[0] - ax)) * inv_sqrt2)
        for p in arm_pts
        if abs((p[0] - ax) - (p[2] - az)) * inv_sqrt2 <= 1.25
    ]
    hull = convex_hull_2d(band if len(band) >= 8 else [(p[1] - ay - 9.0, (p[2] + p[0]) * inv_sqrt2) for p in arm_pts])
    return hull, (ax, az)


def extract_profiles(shape: dict, gt: dict):
    """Extracts Sagittal (Y-Z) and Front (X-Z, including full arms) profile curves."""
    min_z = shape["min_z"]
    h0 = shape["h0"]
    verts = shape["verts"]
    arm_w = shape["arm_w"]
    is_fem = gt.get("is_female", True)

    sagittal_mid_front = []
    sagittal_max_front = []
    sagittal_mid_back = []
    sagittal_min_back = []
    front_outer_right = []
    front_outer_left = []
    front_inner_right = []
    front_inner_left = []
    right_arm_outer = []
    right_arm_inner = []
    left_arm_outer = []
    left_arm_inner = []

    z_crotch = gt["z_landmarks"]["crotch"]

    for step in range(4, 98, 1):
        zn = step * 0.01
        z_lo = min_z + (zn - 0.008) * h0
        z_hi = min_z + (zn + 0.008) * h0
        z_val = min_z + zn * h0

        # Torso & legs band (excluding free-hanging arm shaft)
        band = [
            (x, y, z)
            for i, (x, y, z) in enumerate(verts)
            if z_lo <= z <= z_hi and arm_w[i] <= 0.35 and abs(x) <= 22.0
        ]
        torso_max_x = 8.5
        torso_min_x = -8.5
        if band:
            xs = [p[0] for p in band]
            ys = [p[1] for p in band if abs(p[0]) <= 12.5] or [p[1] for p in band]
            torso_max_x = max(xs)
            torso_min_x = min(xs)
            front_outer_right.append((torso_max_x, z_val))
            front_outer_left.append((torso_min_x, z_val))

            if zn < z_crotch - 0.01:
                r_leg = [p[0] for p in band if p[0] > 0.15]
                l_leg = [p[0] for p in band if p[0] < -0.15]
                if r_leg and l_leg:
                    front_inner_right.append((min(r_leg), z_val))
                    front_inner_left.append((max(l_leg), z_val))

            if zn >= z_crotch:
                mid_x_tol = 3.5 if (not is_fem and zn < 0.56) else 1.3
                f_mid = [p[1] for p in band if abs(p[0]) <= mid_x_tol and p[1] > 0]
                b_mid = [p[1] for p in band if abs(p[0]) <= 1.3 and p[1] < 0]
                sagittal_max_front.append((max(ys), z_val))
                sagittal_min_back.append((min(ys), z_val))
                if f_mid:
                    sagittal_mid_front.append(
                        (min(f_mid) if (is_fem and 0.76 <= zn <= 0.85) else max(f_mid), z_val)
                    )
                if b_mid:
                    sagittal_mid_back.append((min(b_mid), z_val))
            else:
                r_leg_y = [p[1] for p in band if p[0] > 0.2]
                if r_leg_y:
                    sagittal_max_front.append((max(r_leg_y), z_val))
                    sagittal_min_back.append((min(r_leg_y), z_val))

        # Full left & right arms (up to shoulder cap zn=0.97, clamped at inner edge to torso_max_x/torso_min_x)
        if 0.48 <= zn <= 0.97:
            r_arm = [
                x
                for i, (x, y, z) in enumerate(verts)
                if z_lo <= z <= z_hi and x > 8.5 and arm_w[i] > 0.35
            ]
            l_arm = [
                x
                for i, (x, y, z) in enumerate(verts)
                if z_lo <= z <= z_hi and x < -8.5 and arm_w[i] > 0.35
            ]
            if r_arm and max(r_arm) > torso_max_x:
                right_arm_outer.append((max(r_arm), z_val))
                right_arm_inner.append((max(torso_max_x, min(r_arm)), z_val))
            if l_arm and min(l_arm) < torso_min_x:
                left_arm_outer.append((min(l_arm), z_val))
                left_arm_inner.append((min(torso_min_x, max(l_arm)), z_val))

    return {
        "sagittal_mid_front": sagittal_mid_front,
        "sagittal_max_front": sagittal_max_front,
        "sagittal_mid_back": sagittal_mid_back,
        "sagittal_min_back": sagittal_min_back,
        "front_outer_right": front_outer_right,
        "front_outer_left": front_outer_left,
        "front_inner_right": front_inner_right,
        "front_inner_left": front_inner_left,
        "right_arm_poly": right_arm_outer + list(reversed(right_arm_inner)),
        "left_arm_poly": left_arm_outer + list(reversed(left_arm_inner)),
    }


def render_sagittal_svg(shape: dict, gt: dict, profiles: dict) -> str:
    min_z = shape["min_z"]
    h0 = shape["h0"]
    zl = gt["z_landmarks"]

    w, h = 290, 480

    def tx(y_mesh):
        return 145.0 + y_mesh * 4.1

    def ty(z_mesh):
        return 442.0 - (z_mesh - min_z) * 4.1

    def poly_str(pts):
        return " ".join(f"{tx(p[0]):.1f},{ty(p[1]):.1f}" for p in pts)

    z_bust_val = min_z + zl["bust"] * h0
    z_under_val = min_z + zl["underbust"] * h0
    z_belly_val = min_z + zl["belly"] * h0
    z_hips_val = min_z + zl["hips"] * h0

    bust_sl = slice_triangles_at_z(shape, zl["bust"], max_abs_x=19.5, min_x=-19.5)
    under_sl = slice_triangles_at_z(
        shape, zl["underbust"], max_abs_x=19.5, min_x=-19.5, excl_breast=True, max_breast_w=0.05
    ) or slice_triangles_at_z(shape, zl["underbust"], max_abs_x=19.5, min_x=-19.5)
    belly_sl = slice_triangles_at_z(shape, zl["belly"], max_abs_x=19.5, min_x=-19.5)
    hips_sl = slice_triangles_at_z(
        shape, zl["hips"], max_abs_x=19.5, min_x=-19.5, excl_thigh=False
    )

    apex_y = bust_sl["max_y"]
    sternum_y = min(apex_y, bust_sl["sternum_floor_y"])
    sub_y = under_sl["sternum_floor_y"]
    rib_wall_y = min(apex_y, 0.65 * sternum_y + 0.35 * sub_y)

    r3_fp = sub_y
    r5_fp = belly_sl["mid_front_y"]
    r6_fp = min(hips_sl["mid_front_y"], 0.82 * r3_fp)
    chord_at_belly = 0.60 * r3_fp + 0.40 * r6_fp

    lines = [
        f'<svg width="{w}" height="{h}" viewBox="0 0 {w} {h}" class="blueprint-svg">',
        '<rect width="100%" height="100%" fill="#0f172a" rx="6"/>',
        f'<line x1="{tx(0):.1f}" y1="15" x2="{tx(0):.1f}" y2="{h-15}" stroke="#1e293b" stroke-dasharray="3,3"/>',
        '<text x="10" y="18" fill="#94a3b8" font-size="11" font-weight="bold">1. SAGITTAL (Y-Z) PROFILE</text>',
        '<text x="14" y="34" fill="#64748b" font-size="10">← Back (-Y)</text>',
        f'<text x="{w-75}" y="34" fill="#64748b" font-size="10">Front (+Y) →</text>',
    ]

    outer_pts = profiles["sagittal_max_front"] + list(reversed(profiles["sagittal_min_back"]))
    if outer_pts:
        lines.append(
            f'<polygon points="{poly_str(outer_pts)}" fill="#1e293b" fill-opacity="0.7" stroke="#64748b" stroke-width="1.5"/>'
        )

    if profiles["sagittal_mid_front"]:
        lines.append(
            f'<polyline points="{poly_str(profiles["sagittal_mid_front"])}" fill="none" stroke="#38bdf8" stroke-width="1.5" stroke-dasharray="3,2"/>'
        )
    if profiles["sagittal_mid_back"]:
        lines.append(
            f'<polyline points="{poly_str(profiles["sagittal_mid_back"])}" fill="none" stroke="#38bdf8" stroke-width="1.2" stroke-dasharray="2,2"/>'
        )

    is_fem = gt.get("is_female", True)
    chest_slice_label = "Bust" if is_fem else "Chest"
    slice_defs = [
        (chest_slice_label, zl["bust"], "#f472b6"),
        ("Under", zl["underbust"], "#22d3ee"),
        ("Waist", zl["waist"], "#4ade80"),
        ("Belly", zl["belly"], "#facc15"),
        ("Hips", zl["hips"], "#fb923c"),
        ("Crotch", zl["crotch"], "#94a3b8"),
        ("Thigh", zl["thigh"], "#c084fc"),
        ("Calf", 0.19, "#38bdf8"),
    ]
    for label, zn, color in slice_defs:
        zy = ty(min_z + zn * h0)
        lines.append(
            f'<line x1="25" y1="{zy:.1f}" x2="{w-15}" y2="{zy:.1f}" stroke="{color}" stroke-opacity="0.35" stroke-width="1"/>'
        )
        lines.append(f'<text x="6" y="{zy+3:.1f}" fill="{color}" font-size="9">{label}</text>')

    if is_fem:
        lines.append(
            f'<line x1="{tx(sternum_y):.1f}" y1="{ty(z_bust_val):.1f}" x2="{tx(sub_y):.1f}" y2="{ty(z_under_val):.1f}" stroke="#22d3ee" stroke-width="2.2"/>'
        )
        lines.append(
            f'<circle cx="{tx(rib_wall_y):.1f}" cy="{ty(z_bust_val):.1f}" r="3.2" fill="#22d3ee"/>'
        )
        lines.append(
            f'<circle cx="{tx(apex_y):.1f}" cy="{ty(z_bust_val):.1f}" r="3.5" fill="#f472b6"/>'
        )
        lines.append(
            f'<line x1="{tx(rib_wall_y):.1f}" y1="{ty(z_bust_val):.1f}" x2="{tx(apex_y):.1f}" y2="{ty(z_bust_val):.1f}" stroke="#f472b6" stroke-width="2.8"/>'
        )
        lines.append(
            f'<text x="{tx(apex_y)+5:.1f}" y="{ty(z_bust_val)+3:.1f}" fill="#f472b6" font-size="10" font-weight="bold">Cup {gt["ratios"]["bustCup"]:.2f}</text>'
        )
    else:
        lines.append(
            f'<circle cx="{tx(apex_y):.1f}" cy="{ty(z_bust_val):.1f}" r="3.5" fill="#f472b6"/>'
        )
        lines.append(
            f'<text x="{tx(apex_y)+5:.1f}" y="{ty(z_bust_val)+3:.1f}" fill="#f472b6" font-size="10" font-weight="bold">Chest {gt["ratios"]["bustCup"]:.2f}x</text>'
        )

    lines.append(
        f'<line x1="{tx(r3_fp):.1f}" y1="{ty(z_under_val):.1f}" x2="{tx(r6_fp):.1f}" y2="{ty(z_hips_val):.1f}" stroke="#facc15" stroke-width="1.6" stroke-dasharray="4,2"/>'
    )
    lines.append(
        f'<line x1="{tx(chord_at_belly):.1f}" y1="{ty(z_belly_val):.1f}" x2="{tx(r5_fp):.1f}" y2="{ty(z_belly_val):.1f}" stroke="#facc15" stroke-width="2.5"/>'
    )
    lines.append(
        f'<circle cx="{tx(r5_fp):.1f}" cy="{ty(z_belly_val):.1f}" r="3" fill="#facc15"/>'
    )
    bdrop_val = gt["ratios"]["bellyDrop"]
    if abs(bdrop_val) >= 0.008:
        lines.append(
            f'<text x="{tx(max(r5_fp, chord_at_belly))+5:.1f}" y="{ty(z_belly_val)+3:.1f}" fill="#facc15" font-size="9.5" font-weight="bold">Drop {bdrop_val:+.3f}</text>'
        )

    if is_fem:
        lines.append(
            f'<text x="10" y="{h-28}" fill="#22d3ee" font-size="9">■ Cyan: Sternum→Underbust Rib Wall | Dashed: Midline Skin</text>'
        )
        lines.append(
            f'<text x="10" y="{h-17}" fill="#f472b6" font-size="9">■ Pink: Pure Breast Projection (no ribcage width)</text>'
        )
    else:
        lines.append(
            f'<text x="10" y="{h-28}" fill="#38bdf8" font-size="9">■ Cyan Dashed: Midline Skin (Sternum / Linea Alba / Spine)</text>'
        )
        lines.append(
            f'<text x="10" y="{h-17}" fill="#f472b6" font-size="9">■ Pink: Chest / Pectoral-Lat Girth Ratio vs. Underbust</text>'
        )
    lines.append(
        f'<text x="10" y="{h-6}" fill="#facc15" font-size="9">■ Yellow: Underbust→Hip Flat-Stomach Reference Chord</text>'
    )
    lines.append("</svg>")
    return "\n".join(lines)


def render_front_svg(shape: dict, gt: dict, profiles: dict, arm_center: tuple) -> str:
    min_z = shape["min_z"]
    h0 = shape["h0"]
    zl = gt["z_landmarks"]
    m_in = gt["measurements_in"]

    w, h = 285, 480
    cx = w * 0.5

    def tx(x_mesh):
        return cx + x_mesh * 4.1

    def ty(z_mesh):
        return 442.0 - (z_mesh - min_z) * 4.1

    def poly_str(pts):
        return " ".join(f"{tx(p[0]):.1f},{ty(p[1]):.1f}" for p in pts)

    lines = [
        f'<svg width="{w}" height="{h}" viewBox="0 0 {w} {h}" class="blueprint-svg">',
        '<rect width="100%" height="100%" fill="#0f172a" rx="6"/>',
        f'<line x1="{cx:.1f}" y1="15" x2="{cx:.1f}" y2="{h-15}" stroke="#1e293b" stroke-dasharray="3,3"/>',
        '<text x="10" y="18" fill="#94a3b8" font-size="11" font-weight="bold">2. FRONT (X-Z) SILHOUETTE</text>',
    ]

    # Draw full left & right arms first (with air gap preserved between waist and arms)
    for arm_poly in (profiles["right_arm_poly"], profiles["left_arm_poly"]):
        if arm_poly:
            lines.append(
                f'<polygon points="{poly_str(arm_poly)}" fill="#1e293b" fill-opacity="0.55" stroke="#475569" stroke-width="1.3"/>'
            )

    outer_pts = profiles["front_outer_right"] + list(reversed(profiles["front_outer_left"]))
    if outer_pts:
        lines.append(
            f'<polygon points="{poly_str(outer_pts)}" fill="#1e293b" fill-opacity="0.85" stroke="#64748b" stroke-width="1.5"/>'
        )
    if profiles["front_inner_right"] and profiles["front_inner_left"]:
        inner_pts = profiles["front_inner_right"] + list(reversed(profiles["front_inner_left"]))
        lines.append(
            f'<polygon points="{poly_str(inner_pts)}" fill="#0f172a" stroke="#475569" stroke-width="1"/>'
        )

    # Draw perpendicular 45-degree Upper Arm slice bar (Ring 9) on the right side of the SVG (x > 0)
    ax, az = arm_center
    arm_x1, arm_z1 = ax - 2.6, az - 2.6
    arm_x2, arm_z2 = ax + 2.6, az + 2.6
    lines.append(
        f'<line x1="{tx(arm_x1):.1f}" y1="{ty(arm_z1):.1f}" x2="{tx(arm_x2):.1f}" y2="{ty(arm_z2):.1f}" stroke="#f43f5e" stroke-width="2.2"/>'
    )
    lines.append(
        f'<text x="8" y="{ty(az)+3:.1f}" fill="#f43f5e" font-size="9.5" font-weight="bold">Arm {m_in["arm"]:.1f}"</text>'
    )
    lines.append(
        f'<line x1="{tx(-ax-2.6):.1f}" y1="{ty(arm_z2):.1f}" x2="{tx(-ax+2.6):.1f}" y2="{ty(arm_z1):.1f}" stroke="#f43f5e" stroke-width="2.2"/>'
    )

    ring_bars = [
        ("Shldr", 0.935, f"{m_in['shoulder_width']:.1f}\"", "#a855f7"),
        ("Bust", zl["bust"], f"{m_in['chest']:.1f}\"", "#f472b6"),
        ("Under", zl["underbust"], f"{m_in['underbust']:.1f}\"", "#22d3ee"),
        ("Waist", zl["waist"], f"{m_in['waist']:.1f}\"", "#4ade80"),
        ("Belly", zl["belly"], f"{m_in['belly']:.1f}\"", "#facc15"),
        ("Hips", zl["hips"], f"{m_in['hips']:.1f}\"", "#fb923c"),
        ("Thigh", zl["thigh"], f"{m_in['thigh']:.1f}\"", "#c084fc"),
        ("Calf", 0.19, f"{m_in['calf']:.1f}\"", "#38bdf8"),
    ]
    for label, zn, val_str, color in ring_bars:
        zy = ty(min_z + zn * h0)
        lines.append(
            f'<line x1="45" y1="{zy:.1f}" x2="{w-42}" y2="{zy:.1f}" stroke="{color}" stroke-width="1.4" stroke-opacity="0.75"/>'
        )
        lines.append(
            f'<text x="{w-40}" y="{zy+3:.1f}" fill="{color}" font-size="9.5" font-weight="bold">{val_str}</text>'
        )

    lines.append(
        f'<text x="10" y="{h-8}" fill="#94a3b8" font-size="9.5">WHR={gt["ratios"]["whr"]:.2f} | V-Taper={gt["ratios"]["vtaper"]:.2f}</text>'
    )
    lines.append("</svg>")
    return "\n".join(lines)


def render_cross_section_svg(
    title: str,
    poly_a: list,
    hull_a: list,
    color_a: str,
    label_a: str,
    poly_b: list,
    hull_b: list,
    color_b: str,
    label_b: str,
    poly_c: list = None,
    color_c: str = "",
    label_c: str = "",
) -> str:
    w, h = 250, 153
    cx, cy = w * 0.5, h * 0.50
    scale = 5.0

    def tx(x_mesh):
        return cx + x_mesh * scale

    def ty(y_mesh):
        return cy - y_mesh * scale

    def poly_str(pts):
        return " ".join(f"{tx(p[0]):.1f},{ty(p[1]):.1f}" for p in pts)

    lines = [
        f'<svg width="{w}" height="{h}" viewBox="0 0 {w} {h}" class="blueprint-svg">',
        '<rect width="100%" height="100%" fill="#0f172a" rx="6"/>',
        f'<line x1="{cx:.1f}" y1="15" x2="{cx:.1f}" y2="{h-15}" stroke="#1e293b" stroke-dasharray="2,2"/>',
        f'<line x1="15" y1="{cy:.1f}" x2="{w-15}" y2="{cy:.1f}" stroke="#1e293b" stroke-dasharray="2,2"/>',
        f'<text x="8" y="14" fill="#94a3b8" font-size="10" font-weight="bold">{html.escape(title)}</text>',
        f'<text x="{w-52}" y="14" fill="#64748b" font-size="8.5">↑ Front (+Y)</text>',
    ]

    if poly_a:
        lines.append(
            f'<polygon points="{poly_str(poly_a)}" fill="{color_a}" fill-opacity="0.18" stroke="{color_a}" stroke-width="1.6"/>'
        )
    if hull_a:
        lines.append(
            f'<polygon points="{poly_str(hull_a)}" fill="none" stroke="{color_a}" stroke-width="1" stroke-dasharray="3,2"/>'
        )
    if poly_b:
        lines.append(
            f'<polygon points="{poly_str(poly_b)}" fill="{color_b}" fill-opacity="0.22" stroke="{color_b}" stroke-width="1.6"/>'
        )
    if hull_b:
        lines.append(
            f'<polygon points="{poly_str(hull_b)}" fill="none" stroke="#f8fafc" stroke-width="1" stroke-dasharray="2,2" stroke-opacity="0.6"/>'
        )
    if poly_c:
        lines.append(
            f'<polygon points="{poly_str(poly_c)}" fill="{color_c}" fill-opacity="0.22" stroke="{color_c}" stroke-width="1.6"/>'
        )

    if label_c:
        lines.append(
            f'<text x="8" y="{h-17}" fill="{color_a}" font-size="8.8">■ {html.escape(label_a)}  <tspan fill="{color_b}">■ {html.escape(label_b)}</tspan></text>'
        )
        lines.append(f'<text x="8" y="{h-5}" fill="{color_c}" font-size="8.8">■ {html.escape(label_c)}</text>')
    else:
        lines.append(f'<text x="8" y="{h-17}" fill="{color_a}" font-size="9">■ {html.escape(label_a)}</text>')
        lines.append(f'<text x="8" y="{h-5}" fill="{color_b}" font-size="9">■ {html.escape(label_b)}</text>')
    lines.append("</svg>")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description="Generates an interactive HTML/SVG visual verification report for Ground Truth & C++ BodyLanguage.dll."
    )
    parser.add_argument("--dll", default=DEFAULT_DLL_PATH)
    parser.add_argument("--ground-truth", default=GROUND_TRUTH_JSON)
    parser.add_argument(
        "--output",
        default=os.path.join(os.path.dirname(__file__), "body_visual_report.html"),
    )
    args = parser.parse_args()

    with open(args.ground_truth, "r", encoding="utf-8") as f:
        gt_data = json.load(f)

    bridge = BodyLanguageCppBridge(args.dll)

    cards_by_type = {}
    for key, gt in gt_data["samples"].items():
        body_type = key.split("/")[0] if "/" in key else "other"
        nif_path = os.path.join(REPO_ROOT, gt["nif_path"])
        is_fem = gt.get("is_female", "female" in os.path.basename(nif_path).lower())
        base_tpl = BASE_3BA_NIF if is_fem else (BASE_HIMBO_NIF if os.path.exists(BASE_HIMBO_NIF) else "")
        shape = load_nif_mesh(nif_path)
        profiles = extract_profiles(shape, gt)
        arm_hull, arm_center = extract_arm_cross_section(shape)
        zl = gt["z_landmarks"]
        m_in = gt["measurements_in"]
        r_gt = gt["ratios"]

        bust_poly, bust_hull = extract_slice_polygons_at_z(
            shape, zl["bust"], max_abs_x=19.5, min_x=-19.5
        )
        under_poly, under_hull = extract_slice_polygons_at_z(
            shape, zl["underbust"], max_abs_x=19.5, min_x=-19.5, excl_breast=True, max_breast_w=0.05
        )
        if not under_poly:
            under_poly, under_hull = extract_slice_polygons_at_z(
                shape, zl["underbust"], max_abs_x=19.5, min_x=-19.5
            )
        waist_poly, waist_hull = extract_slice_polygons_at_z(
            shape, zl["waist"], max_abs_x=19.5, min_x=-19.5, excl_breast=True, max_breast_w=0.05
        )
        under_front_crease_y = min(
            (y for x, y in under_poly if abs(x) <= 1.5 and y > 0),
            default=max((y for x, y in under_hull), default=8.0),
        )
        hips_poly, hips_hull = extract_slice_polygons_at_z(
            shape,
            zl["hips"],
            max_abs_x=19.5,
            min_x=-19.5,
            excl_thigh=False,
            max_front_y=0.82 * under_front_crease_y,
        )
        thigh_poly, thigh_hull = extract_slice_polygons_at_z(
            shape, zl["thigh"], max_abs_x=19.5, min_x=0.3
        )
        calf_poly, calf_hull = extract_slice_polygons_at_z(
            shape, 0.19, max_abs_x=19.5, min_x=0.3, allow_calf=True
        )

        cpp_dir = bridge.evaluate_nif(REPO_ROOT, nif_path, base_template_nif="", is_female=is_fem, is_naked=True)
        cpp_obd = bridge.evaluate_nif(
            REPO_ROOT, nif_path, base_template_nif=base_tpl, is_female=is_fem, is_naked=True
        )

        sag_svg = render_sagittal_svg(shape, gt, profiles)
        front_svg = render_front_svg(shape, gt, profiles, arm_center)
        cs_chest_svg = render_cross_section_svg(
            "3A. TOP-DOWN: BUST vs. UNDERBUST" if is_fem else "3A. TOP-DOWN: CHEST vs. UNDERBUST",
            bust_poly,
            bust_hull,
            "#f472b6",
            f"Bust ({m_in['chest']:.1f}\")" if is_fem else f"Chest ({m_in['chest']:.1f}\")",
            under_poly,
            under_hull,
            "#22d3ee",
            f"Underbust Ribcage ({m_in['underbust']:.1f}\")",
        )
        cs_hips_svg = render_cross_section_svg(
            "3B. TOP-DOWN: HIPS vs. WAIST",
            hips_poly,
            hips_hull,
            "#fb923c",
            f"Hips/Glutes ({m_in['hips']:.1f}\")",
            waist_poly,
            waist_hull,
            "#4ade80",
            f"Natural Waist ({m_in['waist']:.1f}\")",
        )
        cs_limbs_svg = render_cross_section_svg(
            "3C. LIMBS: THIGH, CALF & ARM",
            thigh_poly,
            thigh_hull,
            "#c084fc",
            f"Thigh ({m_in['thigh']:.1f}\")",
            calf_poly,
            calf_hull,
            "#38bdf8",
            f"Calf ({m_in['calf']:.1f}\")",
            arm_hull,
            "#f43f5e",
            f"Upper Arm ({m_in['arm']:.1f}\") [shifted left]",
        )

        kw_dir = cpp_dir["keywords"]
        kw_obd = cpp_obd["keywords"]
        m_dir = cpp_dir["measurements_in"]
        m_obd = cpp_obd["measurements_in"]
        r_dir = cpp_dir["ratios"]
        r_obd = cpp_obd["ratios"]
        bproj_dir_in = max(0.0, (r_dir["bustCup"] - 1.0) / 6.0) * (167.6 / 2.54)
        bproj_obd_in = max(0.0, (r_obd["bustCup"] - 1.0) / 6.0) * (167.6 / 2.54)
        bproj_gt_in = m_in.get("breast_proj", max(0.0, (r_gt["bustCup"] - 1.0) / 6.0) * (167.6 / 2.54))

        def kw_get(kw_map, fem_key, male_key):
            return kw_map.get(fem_key, kw_map.get(male_key, ""))

        dir_frame = kw_get(kw_dir, "frame", "shoulder_breadth")
        obd_frame = kw_get(kw_obd, "frame", "shoulder_breadth")
        dir_chest = kw_get(kw_dir, "breasts_volume", "chest")
        obd_chest = kw_get(kw_obd, "breasts_volume", "chest")
        dir_waist = kw_get(kw_dir, "waist", "midsection")
        obd_waist = kw_get(kw_obd, "waist", "midsection")
        dir_belly = kw_get(kw_dir, "belly", "midsection")
        obd_belly = kw_get(kw_obd, "belly", "midsection")
        dir_hips = kw_get(kw_dir, "butt_hips", "glutes")
        obd_hips = kw_get(kw_obd, "butt_hips", "glutes")
        dir_bmi_kw = kw_dir.get("physique_condition", "") if is_fem else f"{kw_dir.get('stature_frame', '')} / {kw_dir.get('physique_condition', '')}"
        obd_bmi_kw = kw_obd.get("physique_condition", "") if is_fem else f"{kw_obd.get('stature_frame', '')} / {kw_obd.get('physique_condition', '')}"
        cup_label = "Bust Cup (1 + 6·ΔY/H₀)" if is_fem else "Chest-to-Underbust Ratio"

        chest_row_html = (
            f'<tr><td>Anterior Breast Proj (ΔY)</td><td>{bproj_gt_in:.2f}"</td><td>{bproj_dir_in:.2f}"</td><td>{bproj_obd_in:.2f}"</td></tr>'
            if is_fem
            else f'<tr><td>Upper Chest Girth</td><td>{m_in["chest"]:.1f}"</td><td>{m_dir["chest"]:.1f}"</td><td>{m_obd["chest"]:.1f}"</td></tr>'
        )

        card = f"""
        <div class="preset-card" data-body-type="{html.escape(body_type)}">
            <div class="preset-header">
                <h2>{html.escape(key)}</h2>
                <span class="badge">C++ Condition (Runtime Morph): <b>{html.escape(kw_obd['physique_condition'])}</b> | Frame: <b>{html.escape(obd_frame)}</b> | {"Breasts" if is_fem else "Chest"}: <b>{html.escape(obd_chest)}</b> | {"Waist" if is_fem else "Midsection"}: <b>{html.escape(obd_waist)}</b> | {"Belly" if is_fem else "Stature"}: <b>{html.escape(obd_belly if is_fem else kw_obd.get("stature_frame", ""))}</b> | {"Hips/Butt" if is_fem else "Glutes"}: <b>{html.escape(obd_hips)}</b> | Legs: <b>{html.escape(kw_obd['legs'])}</b> | Arms: <b>{html.escape(kw_obd['arms'])}</b></span>
            </div>
            <div class="blueprint-grid">
                <div>{sag_svg}</div>
                <div>{front_svg}</div>
                <div class="cs-stack">
                    {cs_chest_svg}
                    {cs_hips_svg}
                    {cs_limbs_svg}
                </div>
                <div class="metrics-panel">
                    <table class="metrics-table">
                        <thead>
                            <tr>
                                <th>Metric</th>
                                <th>Ground Truth (3D Tris)</th>
                                <th>C++ DLL (Direct NIF)</th>
                                <th>C++ DLL (Runtime Morph)</th>
                            </tr>
                        </thead>
                        <tbody>
                            <tr><td>Shoulder Width</td><td>{m_in['shoulder_width']:.1f}" ({r_gt['shldr']:.2f})</td><td>{m_dir['shoulder_width']:.1f}" ({r_dir['shldr']:.2f} {dir_frame})</td><td>{m_obd['shoulder_width']:.1f}" ({r_obd['shldr']:.2f} {obd_frame})</td></tr>
                            {chest_row_html}
                            <tr><td>Underbust</td><td>{m_in['underbust']:.1f}"</td><td>{m_dir['underbust']:.1f}"</td><td>{m_obd['underbust']:.1f}"</td></tr>
                            <tr><td>Natural Waist</td><td>{m_in['waist']:.1f}"</td><td>{m_dir['waist']:.1f}"</td><td>{m_obd['waist']:.1f}"</td></tr>
                            <tr><td>Mid Belly</td><td>{m_in['belly']:.1f}"</td><td>{m_dir['belly']:.1f}"</td><td>{m_obd['belly']:.1f}"</td></tr>
                            <tr><td>Hips / Glutes</td><td>{m_in['hips']:.1f}"</td><td>{m_dir['hips']:.1f}"</td><td>{m_obd['hips']:.1f}"</td></tr>
                            <tr><td>Upper Thigh</td><td>{m_in['thigh']:.1f}"</td><td>{m_dir['thigh']:.1f}"</td><td>{m_obd['thigh']:.1f}"</td></tr>
                            <tr><td>Lower Leg (Calf)</td><td>{m_in['calf']:.1f}"</td><td>{m_dir['calf']:.1f}"</td><td>{m_obd['calf']:.1f}"</td></tr>
                            <tr><td>Upper Arm (Bicep)</td><td>{m_in['arm']:.1f}"</td><td>{m_dir['arm']:.1f}"</td><td>{m_obd['arm']:.1f}"</td></tr>
                            <tr class="ratio-row"><td><b>{cup_label}</b></td><td><b>{r_gt['bustCup']:.2f}</b></td><td><b>{r_dir['bustCup']:.2f}</b> ({dir_chest})</td><td><b>{r_obd['bustCup']:.2f}</b> ({obd_chest})</td></tr>
                            <tr class="ratio-row"><td><b>Waist-to-Hip (WHR)</b></td><td><b>{r_gt['whr']:.2f}</b></td><td><b>{r_dir['whr']:.2f}</b> ({dir_waist})</td><td><b>{r_obd['whr']:.2f}</b> ({obd_waist})</td></tr>
                            <tr class="ratio-row"><td><b>Belly Drop</b></td><td><b>{r_gt['bellyDrop']:+.3f}</b></td><td><b>{r_dir['bellyDrop']:+.3f}</b> ({dir_belly})</td><td><b>{r_obd['bellyDrop']:+.3f}</b> ({obd_belly})</td></tr>
                            <tr class="ratio-row"><td><b>Hips-to-Height</b></td><td><b>{r_gt['hips']:.2f}</b></td><td><b>{r_dir['hips']:.2f}</b> ({dir_hips})</td><td><b>{r_obd['hips']:.2f}</b> ({obd_hips})</td></tr>
                            <tr class="ratio-row"><td><b>Thigh-to-Height (Legs)</b></td><td><b>{r_gt['thigh']:.2f}</b></td><td><b>{r_dir['thigh']:.2f}</b> ({kw_dir['legs']})</td><td><b>{r_obd['thigh']:.2f}</b> ({kw_obd['legs']})</td></tr>
                            <tr class="ratio-row"><td><b>Arm-to-Height (Arms)</b></td><td><b>{r_gt['arm']:.2f}</b></td><td><b>{r_dir['arm']:.2f}</b> ({kw_dir['arms']})</td><td><b>{r_obd['arm']:.2f}</b> ({kw_obd['arms']})</td></tr>
                            <tr class="ratio-row"><td><b>Systemic BMI</b></td><td><b>{r_gt['bmi']:.2f}</b></td><td><b>{r_dir['bmi']:.2f}</b> ({dir_bmi_kw})</td><td><b>{r_obd['bmi']:.2f}</b> ({obd_bmi_kw})</td></tr>
                        </tbody>
                    </table>
                    <div class="desc-box">
                        <div><b>C++ Short Desc (Runtime Morph):</b> {html.escape(cpp_obd['shortDesc'])}</div>
                        <div style="margin-top:6px;"><b>C++ Full Desc (Runtime Morph):</b> {html.escape(cpp_obd['fullDesc'])}</div>
                    </div>
                </div>
            </div>
        </div>
        """
        cards_by_type.setdefault(body_type, []).append(card)

    body_types = list(cards_by_type.keys())
    total_cards = sum(len(v) for v in cards_by_type.values())
    tab_buttons = []
    tab_panels = []
    for idx, bt in enumerate(body_types):
        active_cls = " active" if idx == 0 else ""
        bt_upper = html.escape(bt.upper())
        count = len(cards_by_type[bt])
        tab_buttons.append(
            f'<button class="tab-btn{active_cls}" onclick="switchTab(\'{html.escape(bt)}\', this)">{bt_upper} ({count})</button>'
        )
        tab_panels.append(
            f'<div id="tab-{html.escape(bt)}" class="tab-panel{active_cls}">{"".join(cards_by_type[bt])}</div>'
        )
    tab_buttons.append(
        f'<button class="tab-btn" onclick="switchTab(\'__all__\', this)">ALL ({total_cards})</button>'
    )

    full_html = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>BodyLanguage — 3D Ground Truth & C++ DLL Visual Verification Report</title>
<style>
    body {{
        background: #020617;
        color: #e2e8f0;
        font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
        margin: 0;
        padding: 20px;
    }}
    h1 {{
        margin: 0 0 8px 0;
        font-size: 22px;
        color: #f8fafc;
    }}
    .subtitle {{
        color: #94a3b8;
        font-size: 13.5px;
        margin-bottom: 16px;
    }}
    .tabs-bar {{
        display: flex;
        gap: 10px;
        margin-bottom: 20px;
        border-bottom: 1px solid #1e293b;
        padding-bottom: 12px;
    }}
    .tab-btn {{
        background: #0f172a;
        color: #94a3b8;
        border: 1px solid #334155;
        padding: 8px 18px;
        border-radius: 8px;
        font-size: 13.5px;
        font-weight: 600;
        cursor: pointer;
        transition: all 0.15s ease;
    }}
    .tab-btn:hover {{
        color: #f8fafc;
        border-color: #38bdf8;
    }}
    .tab-btn.active {{
        background: #0284c7;
        color: #f8fafc;
        border-color: #38bdf8;
    }}
    .tab-panel {{
        display: none;
    }}
    .tab-panel.active {{
        display: block;
    }}
    .preset-card {{
        background: #0f172a;
        border: 1px solid #1e293b;
        border-radius: 10px;
        padding: 16px;
        margin-bottom: 22px;
    }}
    .preset-header {{
        display: flex;
        justify-content: space-between;
        align-items: center;
        border-bottom: 1px solid #1e293b;
        padding-bottom: 10px;
        margin-bottom: 14px;
    }}
    .preset-header h2 {{
        margin: 0;
        font-size: 18px;
        color: #38bdf8;
    }}
    .badge {{
        background: #1e293b;
        padding: 5px 12px;
        border-radius: 6px;
        font-size: 12.5px;
        color: #cbd5e1;
    }}
    .badge b {{
        color: #f8fafc;
    }}
    .blueprint-grid {{
        display: grid;
        grid-template-columns: 290px 285px 250px 1fr;
        gap: 14px;
        align-items: start;
    }}
    .cs-stack {{
        display: flex;
        flex-direction: column;
        gap: 10px;
    }}
    .blueprint-svg {{
        border: 1px solid #334155;
        border-radius: 6px;
        display: block;
    }}
    .metrics-table {{
        width: 100%;
        border-collapse: collapse;
        font-size: 12px;
    }}
    .metrics-table th, .metrics-table td {{
        padding: 4px 8px;
        border-bottom: 1px solid #1e293b;
        text-align: left;
    }}
    .metrics-table th {{
        color: #94a3b8;
        background: #1e293b;
    }}
    .ratio-row {{
        background: #111c33;
    }}
    .desc-box {{
        margin-top: 8px;
        padding: 10px;
        background: #1e293b;
        border-radius: 6px;
        font-size: 12px;
        line-height: 1.45;
        color: #cbd5e1;
    }}
</style>
<script>
function switchTab(bt, btn) {{
    document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    document.querySelectorAll('.tab-panel').forEach(p => {{
        if (bt === '__all__' || p.id === 'tab-' + bt) {{
            p.classList.add('active');
        }} else {{
            p.classList.remove('active');
        }}
    }});
}}
</script>
</head>
<body>
    <h1>BodyLanguage — 3D Ground Truth &amp; Compiled C++ DLL Visual Blueprint Report</h1>
    <div class="subtitle">
        Every SVG diagram is sliced directly from the triangles of each sample <code>.nif</code> and paired with live evaluations from <code>BodyLanguage.dll</code>.
        Use the tabs below to switch between body types (e.g. <b>3BA</b>, <b>HIMBO</b>, <b>BHUNP</b>).
    </div>
    <div class="tabs-bar">
        {''.join(tab_buttons)}
    </div>
    {''.join(tab_panels)}
</body>
</html>
"""

    with open(args.output, "w", encoding="utf-8") as f:
        f.write(full_html)

    print(f"[VisualReport] Generated '{args.output}' for {total_cards} presets across {len(body_types)} body type(s).")


if __name__ == "__main__":
    main()
