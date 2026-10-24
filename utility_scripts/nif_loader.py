import math
import struct


def load_nif_mesh(path: str) -> dict:
    """
    Loads a Skyrim SE/AE .nif file and extracts the primary full-height skinned humanoid
    mesh: vertices [(x, y, z)], per-vertex bone weights [[(bone_idx, weight)]],
    bone_names [str], and triangle indices [(i0, i1, i2)].
    """
    with open(path, "rb") as f:
        data = f.read()

    pos = data.find(b"\n") + 1
    ver, endian, user_ver, num_blocks, bs_ver = struct.unpack_from("<IBIII", data, pos)
    pos += 17
    for _ in range(3):
        slen = data[pos]
        pos += 1 + slen
    if bs_ver >= 130:
        pos += 2

    num_block_types = struct.unpack_from("<H", data, pos)[0]
    pos += 2
    block_types = []
    for _ in range(num_block_types):
        tlen = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        block_types.append(data[pos : pos + tlen].decode("ascii", "ignore"))
        pos += tlen

    block_type_idx = struct.unpack_from(f"<{num_blocks}H", data, pos)
    pos += 2 * num_blocks
    block_sizes = struct.unpack_from(f"<{num_blocks}I", data, pos)
    pos += 4 * num_blocks

    num_strings, _ = struct.unpack_from("<II", data, pos)
    pos += 8
    strings = []
    for _ in range(num_strings):
        slen = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        strings.append(data[pos : pos + slen].decode("ascii", "ignore"))
        pos += slen

    num_groups = struct.unpack_from("<I", data, pos)[0]
    pos += 4 + 4 * num_groups

    block_offsets = []
    cur = pos
    for sz in block_sizes:
        block_offsets.append(cur)
        cur += sz

    block_names = {}
    for i in range(num_blocks):
        btype = block_types[block_type_idx[i]]
        if btype in ("NiNode", "BSTriShape", "BSDynamicTriShape", "BSSubIndexTriShape"):
            s_idx = struct.unpack_from("<i", data, block_offsets[i])[0]
            block_names[i] = strings[s_idx] if 0 <= s_idx < len(strings) else ""

    best_shape = None
    for i in range(num_blocks):
        btype = block_types[block_type_idx[i]]
        if btype not in ("BSDismemberSkinInstance", "NiSkinInstance"):
            continue

        sp = block_offsets[i]
        data_ref, part_ref, skel_root, num_bones = struct.unpack_from("<iiiI", data, sp)
        bone_refs = struct.unpack_from(f"<{num_bones}i", data, sp + 16)
        bone_names = [block_names.get(r, "") for r in bone_refs]

        if not (0 <= part_ref < num_blocks and block_types[block_type_idx[part_ref]] == "NiSkinPartition"):
            continue

        pp = block_offsets[part_ref]
        num_parts, data_size, vert_size, vert_desc = struct.unpack_from("<IIIQ", data, pp)
        if vert_size <= 0 or data_size <= 0:
            continue

        num_verts = data_size // vert_size
        if num_verts < 400:
            continue

        v_start = pp + 20
        if num_parts > 1 and (v_start + data_size + 10) <= (pp + block_sizes[part_ref]):
            p0_nv = struct.unpack_from("<H", data, v_start + data_size)[0]
            if 400 <= p0_nv < num_verts:
                num_verts = p0_nv
        verts = []
        vert_weights = [[] for _ in range(num_verts)]
        has_skin_weights = vert_size >= 24

        for v in range(num_verts):
            base = v_start + v * vert_size
            x, y, z = struct.unpack_from("<fff", data, base)
            verts.append((x, y, z))
            if has_skin_weights:
                # In SSE BSVertexDataSSE, 4 half-float weights (8B) and 4 bone indices (4B)
                # occupy the final 12 bytes of each vertex record.
                w0, w1, w2, w3 = struct.unpack_from("<eeee", data, base + vert_size - 12)
                b0, b1, b2, b3 = struct.unpack_from("<BBBB", data, base + vert_size - 4)
                for b_idx, w in ((b0, w0), (b1, w1), (b2, w2), (b3, w3)):
                    if w > 0.001 and b_idx < len(bone_names):
                        vert_weights[v].append((b_idx, float(w)))

        min_z = min(v[2] for v in verts)
        max_z = max(v[2] for v in verts)
        z_span = max_z - min_z
        if z_span < 75.0 or z_span > 135.0 or min_z < -5.0 or max_z < 85.0:
            continue

        # Parse triangles from NiSkinPartition partitions
        tris = []
        c = v_start + data_size
        part_end = pp + block_sizes[part_ref]
        try:
            for _ in range(num_parts):
                if c + 10 > part_end:
                    break
                nv, nt, nb, nstrips, npb = struct.unpack_from("<HHHHH", data, c)
                c += 10
                p_bones = struct.unpack_from(f"<{nb}H", data, c) if nb > 0 else ()
                c += 2 * nb
                has_vmap = data[c]
                c += 1
                vmap = struct.unpack_from(f"<{nv}H", data, c) if has_vmap else None
                if has_vmap:
                    c += 2 * nv
                has_vw = data[c]
                c += 1
                if has_vw:
                    c += 4 * nv * npb
                if nstrips > 0:
                    strip_lens = struct.unpack_from(f"<{nstrips}H", data, c)
                    c += 2 * nstrips
                    has_faces = data[c]
                    c += 1
                    if has_faces:
                        for slen in strip_lens:
                            c += 2 * slen
                else:
                    has_faces = data[c]
                    c += 1
                    faces_off = c
                    if has_faces:
                        c += 6 * nt
                has_bi = data[c]
                c += 1
                if has_bi:
                    c += nv * npb
                c += 2 + 8  # unk_short (2B) + vertex_desc (8B)
                if c + 6 * nt <= part_end:
                    for t in range(nt):
                        t0, t1, t2 = struct.unpack_from("<3H", data, c + 6 * t)
                        if t0 < num_verts and t1 < num_verts and t2 < num_verts:
                            tris.append((t0, t1, t2))
                    c += 6 * nt
                elif has_faces and nstrips == 0:
                    for t in range(nt):
                        t0, t1, t2 = struct.unpack_from("<3H", data, faces_off + 6 * t)
                        if vmap:
                            t0, t1, t2 = vmap[t0], vmap[t1], vmap[t2]
                        if t0 < num_verts and t1 < num_verts and t2 < num_verts:
                            tris.append((t0, t1, t2))
        except Exception:
            pass

        if best_shape is None or len(verts) > len(best_shape["verts"]):
            best_shape = {
                "path": path,
                "verts": verts,
                "tris": tris,
                "bone_names": bone_names,
                "vert_weights": vert_weights,
                "min_z": min_z,
                "max_z": max_z,
                "h0": z_span,
            }

    if best_shape is None:
        raise ValueError(f"No valid humanoid skinned body shape found in '{path}'")

    # Precompute per-vertex bone category weights for fast queries
    b_lower = [b.lower() for b in best_shape["bone_names"]]
    num_v = len(best_shape["verts"])
    arm_w = [0.0] * num_v
    limb_w = [0.0] * num_v
    breast_w = [0.0] * num_v
    thigh_w = [0.0] * num_v
    upperarm_w = [0.0] * num_v
    forearm_w = [0.0] * num_v

    for idx, vw_list in enumerate(best_shape["vert_weights"]):
        aw = lw = bw = tw = uaw = faw = 0.0
        for b_idx, w in vw_list:
            bn = b_lower[b_idx]
            if "upperarm" in bn:
                uaw += w
                aw += w
                lw += w
            elif "forearm" in bn:
                faw += w
                aw += w
                lw += w
            elif "hand" in bn:
                aw += w
                lw += w
            elif "calf" in bn or "foot" in bn:
                lw += w
            if "breast" in bn:
                bw += w
            if "thigh" in bn:
                tw += w
        arm_w[idx] = aw
        limb_w[idx] = lw
        breast_w[idx] = bw
        thigh_w[idx] = tw
        upperarm_w[idx] = uaw
        forearm_w[idx] = faw

    best_shape["arm_w"] = arm_w
    best_shape["limb_w"] = limb_w
    best_shape["breast_w"] = breast_w
    best_shape["thigh_w"] = thigh_w
    best_shape["upperarm_w"] = upperarm_w
    best_shape["forearm_w"] = forearm_w
    return best_shape
