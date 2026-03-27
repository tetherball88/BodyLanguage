import argparse
import json
import math
import os
import struct
import xml.etree.ElementTree as ET

def get_normals_at_offset(data, offset, count):
    """
    Attempts to read 'count' normals starting from 'offset'.
    Verifies if the data looks like a valid SSE vertex buffer.
    """
    normals = []
    vertex_size = 44 # Standard SSE vertex size
    
    # Heuristic: Vertex data usually starts 4-8 bytes after the count
    # Let's try a few start-skips to find the alignment
    for skip in [4, 8, 12, 16]:
        test_normals = []
        start = offset + skip
        try:
            for v in range(count):
                v_off = start + (v * vertex_size)
                # Read packed normals at offset 20 (X, Y, Z, A)
                nx, ny, nz, _ = struct.unpack("4b", data[v_off+20 : v_off+24])
                # Convert to float
                test_normals.append((nx/127.0, ny/127.0, nz/127.0))
            
            # Validation: A real body mesh won't have all zero normals
            valid_count = sum(1 for n in test_normals if abs(n[0]) > 0 or abs(n[1]) > 0)
            if valid_count > (count * 0.5): # If >50% are non-zero, it's likely real
                return test_normals
        except:
            continue
    return None

def build_final_weights(osd_path, offsets, count):
    # Now process the OSD
    weights = {}
    with open(osd_path, "rb") as f:
        f.read(8) # Skip header
        num_sliders = struct.unpack("<I", f.read(4))[0]

        for _ in range(num_sliders):
            name_len = struct.unpack("<B", f.read(1))[0]
            name = f.read(name_len).decode('ascii', errors='ignore')
            num_diffs = struct.unpack("<H", f.read(2))[0]
            
            total_impact = 0.0
            
            for _ in range(num_diffs):
                v_index = struct.unpack("<H", f.read(2))[0]
                dx, dy, dz = struct.unpack("<fff", f.read(12))
                
                total_impact += math.sqrt(dx**2 + dy**2 + dz**2)

            weights[name] = {
                "impact": round(total_impact, 4),
            }

    print("Success! Weights computed.")
    return weights

def build_slider_map(xml_path, osp_path, weights, blacklist_groups=None, blacklist_sliders=None, output_path='SliderMap.json', osd_key_prefix='3BA Ref', osp_ref_set='CBBE 3BA Ref'):
    blacklist_groups = {g.lower() for g in (blacklist_groups or [])}
    blacklist_sliders = {s.lower() for s in (blacklist_sliders or [])}

    # 1. Parse CBBE 3BA.xml (Categories and Display Names)
    xml_data = {}
    blacklisted_slider_names = set()  # tracks sliders excluded by either blacklist
    for category in ET.parse(xml_path).getroot().findall('Category'):
        cat_name = category.get('name')
        if cat_name.lower() in blacklist_groups:
            print(f"[BLACKLIST] Skipping group: {cat_name}")
            for slider in category.findall('Slider'):
                blacklisted_slider_names.add(slider.get('name').lower())
            continue
        for slider in category.findall('Slider'):
            s_name = slider.get('name')
            if s_name.lower() in blacklist_sliders:
                print(f"[BLACKLIST] Skipping slider: {s_name}")
                blacklisted_slider_names.add(s_name.lower())
                continue
            xml_data[s_name] = {
                'group': cat_name,
                'displayName': slider.get('displayname')
            }

    # 2. Parse CBBE 3BA Reference.osp (Defaults and Reference Names)
    osp_data = {}
    for slider_set in ET.parse(osp_path).getroot().findall('SliderSet'):
        set_name = slider_set.get('name')
        for slider in slider_set.findall('Slider'):
            s_name = slider.get('name')
            data_tags = slider.findall('Data')
            data_tag = next((d for d in data_tags if d.get('target', '') == osd_key_prefix), data_tags[0] if data_tags else None)
            ref_name = data_tag.get('name') if data_tag is not None else ""
            # Priority: the configured reference set is the primary standard
            if s_name not in osp_data or set_name == osp_ref_set:
                osp_data[s_name] = {
                    'smallDefault': float(slider.get('small', 0)),
                    'bigDefault': float(slider.get('big', 0)),
                    'referenceName': ref_name
                }

    print(f"[XML] Parsed {len(xml_data)} sliders. Sample keys: {list(xml_data.keys())[:5]}")
    print(f"[OSP] Parsed {len(osp_data)} sliders. Sample keys: {list(osp_data.keys())[:5]}")
    print(f"[Weights] {len(weights)} entries. Sample keys: {list(weights.keys())[:5]}")

    # 3. Merge into Final Map
    no_weight_match = []
    final_map = {}
    weights_lower = {k.lower(): v for k, v in weights.items()}

    for name, info in xml_data.items():
        osp_info = osp_data.get(name, {})
        all_data_names = osp_info.get('allDataNames', [])

        # Primary OSD key: prefix + slider name
        osd_key = osd_key_prefix + name
        if osd_key not in weights:
            osd_key = next((k for k in weights if k.lower() == osd_key.lower()), None)

        if osd_key:
            # Sum impact across all Data entries for this slider (multi-mesh sliders)
            total_impact = weights[osd_key]['impact']
            for data_name in all_data_names:
                if data_name == osd_key or data_name.lower() == osd_key.lower():
                    continue
                w = weights.get(data_name) or weights_lower.get(data_name.lower())
                if w:
                    total_impact += w['impact']
        else:
            # No primary key — sum impact across all Data entries from the OSP
            total_impact = 0.0
            for data_name in all_data_names:
                w = weights.get(data_name) or weights_lower.get(data_name.lower())
                if w:
                    total_impact += w['impact']
            if total_impact == 0.0:
                no_weight_match.append(name)
                print(f"[UNMATCHED] '{name}' (group: {info['group']}) — no OSD entries found, skipping")
                continue

        final_map[name] = {
            "group": info['group'],
            "displayName": info['displayName'],
            "referenceName": osp_info.get('referenceName', ""),
            "smallDefault": osp_info.get('smallDefault', 0.0),
            "bigDefault": osp_info.get('bigDefault', 0.0),
            "sliderName": osd_key if osd_key else osd_key_prefix + name,
            "impact": round(total_impact, 4),
        }

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'w') as f:
        json.dump(final_map, f, indent=4)

    if no_weight_match:
        print(f"[WARN] {len(no_weight_match)} XML sliders had no weight match (in XML but not OSD): {no_weight_match}")
    xml_keys_set = {osd_key_prefix + n for n in xml_data}
    prefix_len = len(osd_key_prefix)
    orphan_weights = [
        k for k in weights
        if k not in xml_keys_set
        and k.lower() not in {x.lower() for x in xml_keys_set}
        and k[prefix_len:].lower() not in blacklisted_slider_names
    ]
    if orphan_weights:
        print(f"[WARN] {len(orphan_weights)} weight entries have no XML slider (in OSD but not XML): {orphan_weights}")
    print(f"[RESULT] Merged {len(final_map)} / {len(xml_data)} sliders into SliderMap.json.")
    return f"Successfully merged {len(final_map)} sliders."

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Parse BodySlide sliders into a SliderMap.')
    parser.add_argument('--type', required=True, help='Body type subfolder name (e.g. 3ba)')
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    config_path = os.path.join(script_dir, args.type, 'config.json')

    with open(config_path, 'r') as f:
        cfg = json.load(f)

    config_dir = os.path.dirname(config_path)
    workspace_dir = os.path.dirname(script_dir)

    def resolve(p):
        return p if os.path.isabs(p) else os.path.join(config_dir, p)

    output_path = os.path.join(
        workspace_dir, 'SKSE', 'Plugins', 'BodyLanguage', 'bodyConfigs', args.type, 'SliderMap.json'
    )

    weights = build_final_weights(
        resolve(cfg['osdPath']),
        cfg['OFFSETS_FROM_SCANNER'],
        cfg['TARGET_COUNT'],
    )
    build_slider_map(
        resolve(cfg['categoriesPath']),
        resolve(cfg['ospPath']),
        weights,
        blacklist_groups=cfg.get('blacklistGroups', []),
        blacklist_sliders=cfg.get('blacklistSliders', []),
        output_path=output_path,
        osd_key_prefix=cfg.get('osdKeyPrefix', '3BA Ref'),
        osp_ref_set=cfg.get('ospReferenceSet', 'CBBE 3BA Ref'),
    )