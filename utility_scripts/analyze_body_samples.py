import argparse
import glob
import json
import math
import os
import time

from nif_loader import load_nif_mesh


def convex_hull_2d(points):
    """Monotone chain 2D convex hull (equivalent to a real tailor's tape measure wrapped around a slice)."""
    pts = sorted(set((round(p[0], 5), round(p[1], 5)) for p in points))
    if len(pts) <= 2:
        return pts

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lower = []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    upper = []
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def polygon_perim_and_area(poly):
    if len(poly) < 3:
        return 0.0, 0.0
    perim = 0.0
    area2 = 0.0
    n = len(poly)
    for i in range(n):
        x1, y1 = poly[i]
        x2, y2 = poly[(i + 1) % n]
        perim += math.hypot(x2 - x1, y2 - y1)
        area2 += x1 * y2 - x2 * y1
    return perim, abs(area2) * 0.5


def slice_triangles_at_z(
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
    """
    Exact 3D triangle-plane intersection at height z = min_z + z_norm * h0.
    Computes both the true tailor's tape-measure perimeter (2D convex hull of the slice)
    and the 72-sector (5-deg) contour perimeter & cross-sectional area.
    """
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

        # Check vertex eligibility
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
        return None

    # 1. Tailor's tape-measure perimeter & area (2D convex hull of exact slice contour)
    tape_hull = convex_hull_2d(intersect_pts)
    tape_perim, tape_area = polygon_perim_and_area(tape_hull)

    # 2. 72-sector (5-deg) radial outer contour (captures cleavage/cleft concavities)
    cx = sum(p[0] for p in intersect_pts) / len(intersect_pts)
    cy = sum(p[1] for p in intersect_pts) / len(intersect_pts)
    sector_pts = []
    for s in range(72):
        a0 = (s * 5.0 - 180.0) * math.pi / 180.0
        a1 = ((s + 1) * 5.0 - 180.0) * math.pi / 180.0
        sec = [p for p in intersect_pts if a0 <= math.atan2(p[1] - cy, p[0] - cx) < a1]
        if sec:
            sector_pts.append(max(sec, key=lambda p: (p[0] - cx) ** 2 + (p[1] - cy) ** 2))
    contour_perim, contour_area = polygon_perim_and_area(sector_pts)

    xs = [p[0] for p in tape_hull]
    ys = [p[1] for p in tape_hull]
    mid_pts = [p for p in intersect_pts if abs(p[0]) <= 1.2]
    front_mid_pts = [p[1] for p in mid_pts if p[1] > 0]
    if not front_mid_pts or z_norm < 0.56:
        front_mid_pts = [p[1] for p in intersect_pts if abs(p[0]) <= 3.5 and p[1] > 0] or front_mid_pts
    mid_front_y = max(front_mid_pts, default=max(ys))
    mid_back_y = min((p[1] for p in mid_pts if p[1] < 0), default=min(ys))
    sternum_floor_y = min((p[1] for p in mid_pts if p[1] > 0), default=mid_front_y)

    return {
        "z_norm": round(z_norm, 4),
        "tape_perim": tape_perim,
        "contour_perim": contour_perim,
        "area": contour_area,
        "width": max(xs) - min(xs),
        "depth": max(ys) - min(ys),
        "max_y": max(ys),
        "min_y": min(ys),
        "mid_front_y": mid_front_y,
        "mid_back_y": mid_back_y,
        "sternum_floor_y": sternum_floor_y,
    }


def analyze_single_body_ground_truth(nif_path: str, ref_height_cm: float = 167.6) -> dict:
    """
    Runs full-mesh, high-resolution triangle-slice ground truth analysis on a single .nif file.
    """
    shape = load_nif_mesh(nif_path)
    verts = shape["verts"]
    min_z = shape["min_z"]
    h0 = shape["h0"]
    arm_w = shape["arm_w"]
    upperarm_w = shape["upperarm_w"]
    forearm_w = shape["forearm_w"]
    cm_per_unit = (ref_height_cm / 120.0) * (102.6 / h0)

    # 1. Exact Crotch Z (excluding touching inner thighs weighted to thigh bones)
    thigh_w = shape["thigh_w"]
    z_crotch = 0.50
    for s in range(440, 560):
        z0, z1 = s * 0.001, (s + 1) * 0.001
        if any(
            z0 <= (z - min_z) / h0 <= z1 and abs(x) < 1.2 and thigh_w[i] <= 0.25
            for i, (x, y, z) in enumerate(verts)
        ):
            z_crotch = round(z0, 4)
            break

    # 2. Exact Shoulder Width (95th percentile acromion/deltoid width in [0.89, 0.96])
    shldr_xs = sorted(
        abs(x) for (x, y, z) in verts if 0.89 <= (z - min_z) / h0 <= 0.96 and x > 0
    )
    shldr_half = shldr_xs[int(len(shldr_xs) * 0.95)] if shldr_xs else 15.0
    shldr_width = 2.0 * shldr_half

    # 3. Exact Chest & Breast Apex Scan ([0.75, 0.85] at 0.0025 steps)
    chest_pts = [
        (x, y, z, i)
        for i, (x, y, z) in enumerate(verts)
        if 0.75 <= (z - min_z) / h0 <= 0.85 and abs(x) <= 18.5 and arm_w[i] <= 0.10
    ]
    r_apex = max((p for p in chest_pts if 1.5 <= p[0] <= 14.5), key=lambda p: p[1])
    l_apex = max((p for p in chest_pts if -14.5 <= p[0] <= -1.5), key=lambda p: p[1])
    apex_y = 0.5 * (r_apex[1] + l_apex[1])
    z_bust = round(((r_apex[2] + l_apex[2]) * 0.5 - min_z) / h0, 4)
    bust_slice = slice_triangles_at_z(shape, z_bust, max_abs_x=19.5, min_x=-19.5)

    # 4. Exact Underbust Slice (inframammary crease below breasts, excluding breast-weighted tris)
    z_under = round(max(0.735, min(0.755, z_bust - 0.045)), 4)
    under_slice = slice_triangles_at_z(
        shape, z_under, max_abs_x=19.5, min_x=-19.5, excl_breast=True, max_breast_w=0.05
    )
    if under_slice is None:
        under_slice = slice_triangles_at_z(
            shape, 0.745, max_abs_x=19.5, min_x=-19.5, excl_breast=True, max_breast_w=0.15
        )

    # Integrate 3D chest/breast volume across [z_under, z_bust + 0.03]
    chest_vol_u3 = 0.0
    dz_step = 0.005 * h0
    zn = z_under
    while zn <= z_bust + 0.03:
        sl = slice_triangles_at_z(shape, zn, max_abs_x=19.5, min_x=-19.5)
        if sl:
            chest_vol_u3 += sl["area"] * dz_step
        zn += 0.005

    # 5. Exact Natural Waist Scan ([0.65, 0.74] at 0.0025 steps -> minimum tape perimeter)
    best_waist = None
    for s in range(260, 297):
        zn = s * 0.0025
        sl = slice_triangles_at_z(
            shape, zn, max_abs_x=19.5, min_x=-19.5, excl_breast=True, max_breast_w=0.05
        )
        if sl and (best_waist is None or sl["tape_perim"] < best_waist["tape_perim"]):
            best_waist = sl

    # 6. Exact Umbilical Belly Slice
    z_belly = round(min(0.675, max(z_crotch + 0.12, best_waist["z_norm"] - 0.050)), 4)
    belly_slice = slice_triangles_at_z(shape, z_belly, max_abs_x=19.5, min_x=-19.5)

    # 7. Exact Pelvis / Hip / Glute Scan ([z_crotch + 0.02, z_crotch + 0.075] -> max posterior -Y glute projection,
    # clamping front +Y to the non-belly pelvic wall 0.82 * under_crease_y so lower-belly overhang is excluded)
    fname_lower = os.path.basename(nif_path).lower()
    is_female = "female" in fname_lower or "male" not in fname_lower
    under_crease_y = under_slice["sternum_floor_y"]
    pelvic_wall_y = 0.82 * under_crease_y
    best_hips = None
    zn = max(0.52, z_crotch + 0.02)
    z_hip_hi = min(0.585, z_crotch + 0.075)
    pelvis_vol_u3 = 0.0
    while zn <= z_hip_hi + 1e-5:
        sl = slice_triangles_at_z(
            shape,
            zn,
            max_abs_x=19.5,
            min_x=-19.5,
            excl_thigh=False,
            max_front_y=pelvic_wall_y,
        )
        if sl:
            pelvis_vol_u3 += sl["area"] * (0.0025 * h0)
            if best_hips is None or sl["min_y"] < best_hips["min_y"]:
                best_hips = sl
        zn += 0.0025

    # 8. Exact Right Upper Thigh Slice (at z_crotch - 0.045, x > 0.3)
    z_thigh = round(max(0.36, z_crotch - 0.045), 4)
    thigh_slice = slice_triangles_at_z(shape, z_thigh, max_abs_x=19.5, min_x=0.3)

    # 9. Exact Right Calf Slice (at z_norm = 0.19, x > 0.3)
    calf_slice = slice_triangles_at_z(shape, 0.19, max_abs_x=16.0, min_x=0.3, allow_calf=True)

    # 10. Exact Upper Arm Perpendicular Slice
    arm_pts = [
        (x, y, z)
        for i, (x, y, z) in enumerate(verts)
        if 0.76 <= (z - min_z) / h0 <= 0.86
        and x > 11.5
        and upperarm_w[i] >= 0.25
        and forearm_w[i] <= 0.15
    ]
    inv_sqrt2 = 0.70710678
    if len(arm_pts) >= 8:
        ax = sum(p[0] for p in arm_pts) / len(arm_pts)
        az = sum(p[2] for p in arm_pts) / len(arm_pts)
        band = [
            (p[1], (p[2] - az + p[0] - ax) * inv_sqrt2)
            for p in arm_pts
            if abs((p[0] - ax) - (p[2] - az)) * inv_sqrt2 <= 1.25
        ]
        arm_hull = convex_hull_2d(band if len(band) >= 8 else [(p[1], (p[2] + p[0]) * inv_sqrt2) for p in arm_pts])
        arm_perim, _ = polygon_perim_and_area(arm_hull)
    else:
        arm_perim = 20.0

    # Ground-truth dimensionless ratios
    sternum_y = min(apex_y, bust_slice["sternum_floor_y"]) / h0
    sub_y = under_crease_y / h0
    apex_y_norm = apex_y / h0
    rib_wall_y = min(apex_y_norm, 0.65 * sternum_y + 0.35 * sub_y)
    breast_proj_units = max(0.0, apex_y_norm - rib_wall_y) * h0
    gt_bust_cup = (
        1.0 + 6.0 * max(0.0, apex_y_norm - rib_wall_y)
        if is_female
        else (bust_slice["tape_perim"] / max(0.1, under_slice["tape_perim"]))
    )

    r3_fp = under_crease_y / h0
    r3_bp = -under_slice["mid_back_y"] / h0
    r5_fp = belly_slice["mid_front_y"] / h0
    r5_bp = -belly_slice["mid_back_y"] / h0
    r6_fp = min(best_hips["mid_front_y"], pelvic_wall_y) / h0
    chord_drop = (0.60 * r3_fp + 0.40 * r6_fp) - r5_fp
    gt_belly_drop = chord_drop

    gt_shldr_ratio = shldr_width / h0
    gt_vtaper = shldr_width / max(0.1, best_waist["width"])
    gt_whr = best_waist["tape_perim"] / best_hips["tape_perim"]
    gt_hips_ratio = best_hips["tape_perim"] / h0
    gt_thigh_ratio = thigh_slice["tape_perim"] / h0
    gt_arm_ratio = arm_perim / h0
    gt_bmi = (
        (
            under_slice["tape_perim"]
            + best_waist["tape_perim"]
            + best_hips["tape_perim"]
            + thigh_slice["tape_perim"]
            + arm_perim
        )
        * 0.20
        / h0
    )

    return {
        "nif_path": os.path.relpath(nif_path, os.getcwd()).replace("\\", "/"),
        "is_female": is_female,
        "h0_units": round(h0, 3),
        "z_landmarks": {
            "crotch": z_crotch,
            "bust": z_bust,
            "underbust": z_under,
            "waist": best_waist["z_norm"],
            "belly": z_belly,
            "hips": best_hips["z_norm"],
            "thigh": z_thigh,
        },
        "perimeters_units": {
            "shoulder_width": round(shldr_width, 2),
            "bust_tape": round(bust_slice["tape_perim"], 2),
            "bust_contour": round(bust_slice["contour_perim"], 2),
            "underbust": round(under_slice["tape_perim"], 2),
            "waist": round(best_waist["tape_perim"], 2),
            "belly": round(belly_slice["tape_perim"], 2),
            "hips": round(best_hips["tape_perim"], 2),
            "thigh": round(thigh_slice["tape_perim"], 2),
            "calf": round(calf_slice["tape_perim"], 2),
            "arm": round(arm_perim, 2),
        },
        "measurements_cm": {
            "shoulder_width": round(shldr_width * cm_per_unit, 1),
            "breast_proj": round(breast_proj_units * cm_per_unit, 2),
            "chest": round(bust_slice["tape_perim"] * cm_per_unit, 1),
            "underbust": round(under_slice["tape_perim"] * cm_per_unit, 1),
            "waist": round(best_waist["tape_perim"] * cm_per_unit, 1),
            "belly": round(belly_slice["tape_perim"] * cm_per_unit, 1),
            "hips": round(best_hips["tape_perim"] * cm_per_unit, 1),
            "thigh": round(thigh_slice["tape_perim"] * cm_per_unit, 1),
            "calf": round(calf_slice["tape_perim"] * cm_per_unit, 1),
            "arm": round(arm_perim * cm_per_unit, 1),
        },
        "measurements_in": {
            "shoulder_width": round(shldr_width * cm_per_unit / 2.54, 1),
            "breast_proj": round(breast_proj_units * cm_per_unit / 2.54, 2),
            "chest": round(bust_slice["tape_perim"] * cm_per_unit / 2.54, 1),
            "underbust": round(under_slice["tape_perim"] * cm_per_unit / 2.54, 1),
            "waist": round(best_waist["tape_perim"] * cm_per_unit / 2.54, 1),
            "belly": round(belly_slice["tape_perim"] * cm_per_unit / 2.54, 1),
            "hips": round(best_hips["tape_perim"] * cm_per_unit / 2.54, 1),
            "thigh": round(thigh_slice["tape_perim"] * cm_per_unit / 2.54, 1),
            "calf": round(calf_slice["tape_perim"] * cm_per_unit / 2.54, 1),
            "arm": round(arm_perim * cm_per_unit / 2.54, 1),
        },
        "volumes_cm3": {
            "chest_segment": round(chest_vol_u3 * (cm_per_unit ** 3), 0),
            "pelvis_segment": round(pelvis_vol_u3 * (cm_per_unit ** 3), 0),
        },
        "ratios": {
            "bmi": round(gt_bmi, 4),
            "shldr": round(gt_shldr_ratio, 4),
            "vtaper": round(gt_vtaper, 4),
            "whr": round(gt_whr, 4),
            "bustCup": round(gt_bust_cup, 4),
            "hips": round(gt_hips_ratio, 4),
            "thigh": round(gt_thigh_ratio, 4),
            "arm": round(gt_arm_ratio, 4),
            "bellyDrop": round(gt_belly_drop, 4),
        },
    }


def main():
    parser = argparse.ArgumentParser(
        description="Heavy offline 3D triangle-slice Ground Truth analyzer for BodyLanguage sample NIFs."
    )
    parser.add_argument(
        "--samples-dir",
        default=os.path.join(os.path.dirname(__file__), "body samples"),
        help="Root directory containing sample .nif files.",
    )
    parser.add_argument(
        "--output",
        default=os.path.join(os.path.dirname(__file__), "body_samples_ground_truth.json"),
        help="Path to write ground-truth JSON dataset.",
    )
    args = parser.parse_args()

    nif_files = sorted(glob.glob(os.path.join(args.samples_dir, "**", "*.nif"), recursive=True))
    if not nif_files:
        print(f"No .nif files found under '{args.samples_dir}'")
        return

    print(f"[GroundTruthAnalyzer] Analyzing {len(nif_files)} sample NIF(s) via exact 3D triangle slicing...")
    t0 = time.time()
    dataset = {"generated_at": time.strftime("%Y-%m-%d %H:%M:%S"), "samples": {}}

    for nif_path in nif_files:
        preset_name = os.path.basename(os.path.dirname(nif_path))
        body_type = os.path.basename(os.path.dirname(os.path.dirname(nif_path)))
        key = f"{body_type}/{preset_name}"
        t_s = time.time()
        gt = analyze_single_body_ground_truth(nif_path)
        elapsed = time.time() - t_s
        dataset["samples"][key] = gt
        m_in = gt["measurements_in"]
        r = gt["ratios"]
        print(
            f"  [OK] {key:30s} ({elapsed:.2f}s) | "
            f"Chest={m_in['chest']:4.1f}\" Under={m_in['underbust']:4.1f}\" Waist={m_in['waist']:4.1f}\" "
            f"Belly={m_in['belly']:4.1f}\" Hips={m_in['hips']:4.1f}\" Thigh={m_in['thigh']:4.1f}\" | "
            f"bustCup={r['bustCup']:.2f} whr={r['whr']:.2f} hips={r['hips']:.2f} bDrop={r['bellyDrop']:+.3f}"
        )

    with open(args.output, "w", encoding="utf-8") as f:
        json.dump(dataset, f, indent=4)

    print(
        f"\n[GroundTruthAnalyzer] Saved {len(dataset['samples'])} ground-truth profiles to '{args.output}' in {time.time() - t0:.2f}s."
    )


if __name__ == "__main__":
    main()
