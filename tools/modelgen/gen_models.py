import json
import os

DATA_DIR = r"c:\Users\varun\OneDrive\Desktop\projects\bedrockmetica\tools\modelgen\node_modules\minecraft-assets\minecraft-assets\data\26.1"
OUT_HPP = r"c:\Users\varun\OneDrive\Desktop\projects\bedrockmetica\src\UI\GeneratedBlockModels.hpp"

with open(os.path.join(DATA_DIR, "blocks_models.json"), "r", encoding="utf-8") as f:
    MODELS = json.load(f)

with open(os.path.join(DATA_DIR, "blocks_states.json"), "r", encoding="utf-8") as f:
    STATES = json.load(f)


def strip_ns(name: str) -> str:
    if name.startswith("minecraft:"):
        name = name[10:]
    if name.startswith("block/"):
        name = name[6:]
    return name


def resolve_model(name: str, seen=None):
    if seen is None:
        seen = set()
    key = strip_ns(name)
    if key in seen or key not in MODELS:
        return None, {}
    seen.add(key)
    raw = MODELS[key]
    parent_elems = None
    parent_tex = {}
    if "parent" in raw:
        parent_elems, parent_tex = resolve_model(raw["parent"], seen)
    elems = raw.get("elements", parent_elems)
    tex = dict(parent_tex)
    if "textures" in raw:
        for k, v in raw["textures"].items():
            tex[k] = v
    return elems, tex


def resolve_tex_var(tex_dict, ref: str) -> str:
    cur = ref
    for _ in range(10):
        if isinstance(cur, dict):
            cur = cur.get("sprite", "")
        if not isinstance(cur, str) or not cur:
            return ""
        if not cur.startswith("#"):
            return strip_ns(cur)
        k = cur[1:]
        if k not in tex_dict:
            return ""
        cur = tex_dict[k]
    return ""


def is_full_cube(elems) -> bool:
    if not elems or len(elems) != 1:
        return False
    e = elems[0]
    if "rotation" in e:
        return False
    fr = e.get("from", [0, 0, 0])
    to = e.get("to", [16, 16, 16])
    if fr != [0, 0, 0] or to != [16, 16, 16]:
        return False
    faces = e.get("faces", {})
    if len(faces) < 6:
        return False
    for d, fd in faces.items():
        if "rotation" in fd and fd["rotation"] != 0:
            return False
        uv = fd.get("uv")
        if uv and uv != [0, 0, 16, 16]:
            return False
    return True


FACE_ORDER = ["up", "down", "north", "south", "east", "west"]
AXIS_MAP = {"x": 1, "y": 2, "z": 3}

cuboid_pool = []
geom_map = {}
geom_list = []

tex_key_pool = []
tex_key_map = {}


def intern_tex(t: str) -> int:
    if not t:
        return 0xFFFF
    if t not in tex_key_map:
        tex_key_map[t] = len(tex_key_pool)
        tex_key_pool.append(t)
    return tex_key_map[t]


def default_uv_for_face(d: str, fr, to):
    x0, y0, z0 = fr
    x1, y1, z1 = to
    if d == "up":
        return [x0, z0, x1, z1]
    if d == "down":
        return [x0, 16 - z1, x1, 16 - z0]
    if d == "north":
        return [16 - x1, 16 - y1, 16 - x0, 16 - y0]
    if d == "south":
        return [x0, 16 - y1, x1, 16 - y0]
    if d == "west":
        return [z0, 16 - y1, z1, 16 - y0]
    if d == "east":
        return [16 - z1, 16 - y1, 16 - z0, 16 - y0]
    return [0, 0, 16, 16]


def build_geom_sig(elems):
    cubs = []
    for e in elems:
        fr = e.get("from", [0, 0, 0])
        to = e.get("to", [16, 16, 16])
        rot = e.get("rotation", {})
        ra = AXIS_MAP.get(rot.get("axis", ""), 0)
        ang = float(rot.get("angle", 0.0))
        orig = rot.get("origin", [8.0, 8.0, 8.0])
        faces = e.get("faces", {})
        ftup = []
        for d in FACE_ORDER:
            if d in faces:
                fd = faces[d]
                uv = fd.get("uv") or default_uv_for_face(d, fr, to)
                frot = int(fd.get("rotation", 0)) // 90
                tint = 1 if "tintindex" in fd else 0
                ftup.append((1, float(uv[0]), float(uv[1]), float(uv[2]), float(uv[3]), frot, tint))
            else:
                ftup.append((0, 0.0, 0.0, 0.0, 0.0, 0, 0))
        cubs.append((
            float(fr[0]), float(fr[1]), float(fr[2]),
            float(to[0]), float(to[1]), float(to[2]),
            ra, ang, float(orig[0]), float(orig[1]), float(orig[2]),
            tuple(ftup)
        ))
    return tuple(cubs)


model_entries = {}

def register_model(model_name: str):
    mn = strip_ns(model_name)
    if mn in model_entries:
        return model_entries[mn]
    elems, tex_dict = resolve_model(mn)
    if not elems or is_full_cube(elems):
        model_entries[mn] = None
        return None
    sig = build_geom_sig(elems)
    if sig not in geom_map:
        start = len(cuboid_pool)
        for c in sig:
            cuboid_pool.append(c)
        geom_map[sig] = len(geom_list)
        geom_list.append((start, len(sig)))
    gid = geom_map[sig]
    # Pick primary side/top/bottom textures for this model
    side_t = (
        resolve_tex_var(tex_dict, "#texture")
        or resolve_tex_var(tex_dict, "#all")
        or resolve_tex_var(tex_dict, "#side")
        or resolve_tex_var(tex_dict, "#lantern")
        or resolve_tex_var(tex_dict, "#torch")
        or resolve_tex_var(tex_dict, "#candle")
        or resolve_tex_var(tex_dict, "#cross")
        or resolve_tex_var(tex_dict, "#plant")
        or resolve_tex_var(tex_dict, "#pane")
        or resolve_tex_var(tex_dict, "#wall")
        or resolve_tex_var(tex_dict, "#particle")
    )
    top_t = (
        resolve_tex_var(tex_dict, "#top")
        or resolve_tex_var(tex_dict, "#end")
        or side_t
    )
    bot_t = (
        resolve_tex_var(tex_dict, "#bottom")
        or resolve_tex_var(tex_dict, "#end")
        or side_t
    )
    entry = (gid, intern_tex(side_t), intern_tex(top_t), intern_tex(bot_t))
    model_entries[mn] = entry
    return entry


def pick_default_apply(st_obj):
    if "variants" in st_obj:
        variants = st_obj["variants"]
        for k in ["", "hanging=false", "facing=up,powered=false", "facing=north", "facing=up", "lit=true", "lit=false"]:
            if k in variants:
                v = variants[k]
                return v[0] if isinstance(v, list) else v
        first_v = next(iter(variants.values()))
        return first_v[0] if isinstance(first_v, list) else first_v
    if "multipart" in st_obj and st_obj["multipart"]:
        first = st_obj["multipart"][0]["apply"]
        return first[0] if isinstance(first, list) else first
    return None


block_default_map = []
for bname, sobj in sorted(STATES.items()):
    ap = pick_default_apply(sobj)
    if not ap or "model" not in ap:
        continue
    mname = strip_ns(ap["model"])
    ent = register_model(mname)
    if ent is not None:
        rx = int(ap.get("x", 0)) // 90
        ry = int(ap.get("y", 0)) // 90
        block_default_map.append((bname, ent[0], ent[1], ent[2], ent[3], rx, ry))

# Also register key alternate variant models (hanging lantern, wall_torch, etc.)
extra_models = []
for mname in sorted(MODELS.keys()):
    if any(k in mname for k in ["hanging", "wall_torch", "wall_lantern", "_slab", "_stairs", "_wall", "_fence", "_trapdoor", "_door", "_gate", "_shelf", "leaf_litter", "wildflowers", "pink_petals", "lightning_rod", "end_rod", "chain"]):
        ent = register_model(mname)
        if ent is not None:
            extra_models.append((mname, ent[0], ent[1], ent[2], ent[3]))

lines = []
lines.append("#pragma once")
lines.append("#include <cstdint>")
lines.append("#include <cstring>")
lines.append("#include <string>")
lines.append("")
lines.append("namespace GeneratedBlockModels {")
lines.append("")
lines.append("struct BakedFace {")
lines.append("  uint8_t valid;")
lines.append("  float u0, v0, u1, v1;")
lines.append("  uint8_t rot;")
lines.append("  uint8_t tint;")
lines.append("};")
lines.append("")
lines.append("struct BakedCuboid {")
lines.append("  float fx0, fy0, fz0;")
lines.append("  float fx1, fy1, fz1;")
lines.append("  uint8_t rotAxis;")
lines.append("  float rotAngle;")
lines.append("  float ox, oy, oz;")
lines.append("  BakedFace faces[6];")
lines.append("};")
lines.append("")
lines.append("struct BakedGeom {")
lines.append("  uint16_t start;")
lines.append("  uint16_t count;")
lines.append("};")
lines.append("")
lines.append("struct BakedBlockModel {")
lines.append("  const char *name;")
lines.append("  uint16_t geomIdx;")
lines.append("  uint16_t sideTex;")
lines.append("  uint16_t topTex;")
lines.append("  uint16_t botTex;")
lines.append("  uint8_t rotX;")
lines.append("  uint8_t rotY;")
lines.append("};")
lines.append("")
lines.append(f"inline constexpr const char *kTexKeys[{len(tex_key_pool) + 1}] = {{")
for tk in tex_key_pool:
    lines.append(f'  "{tk}",')
lines.append('  ""')
lines.append("};")
lines.append("")
lines.append(f"inline constexpr BakedCuboid kCuboids[{len(cuboid_pool)}] = {{")
for c in cuboid_pool:
    fstrs = []
    for f in c[11]:
        fstrs.append(f"{{{f[0]},{f[1]:.2f}f,{f[2]:.2f}f,{f[3]:.2f}f,{f[4]:.2f}f,{f[5]},{f[6]}}}")
    lines.append(
        f"  {{{c[0]:.2f}f,{c[1]:.2f}f,{c[2]:.2f}f,{c[3]:.2f}f,{c[4]:.2f}f,{c[5]:.2f}f,{c[6]},{c[7]:.1f}f,{c[8]:.2f}f,{c[9]:.2f}f,{c[10]:.2f}f,{{{','.join(fstrs)}}}}},"
    )
lines.append("};")
lines.append("")
lines.append(f"inline constexpr BakedGeom kGeoms[{len(geom_list)}] = {{")
for g in geom_list:
    lines.append(f"  {{{g[0]},{g[1]}}},")
lines.append("};")
lines.append("")
lines.append(f"inline constexpr BakedBlockModel kBlocks[{len(block_default_map)}] = {{")
for b in block_default_map:
    lines.append(f'  {{"{b[0]}",{b[1]},{b[2]},{b[3]},{b[4]},{b[5]},{b[6]}}},')
lines.append("};")
lines.append("")
lines.append(f"inline constexpr BakedBlockModel kVariantModels[{len(extra_models)}] = {{")
for m in extra_models:
    lines.append(f'  {{"{m[0]}",{m[1]},{m[2]},{m[3]},{m[4]},0,0}},')
lines.append("};")
lines.append("")
lines.append("inline const BakedBlockModel *FindBlock(const std::string &name) {")
lines.append("  int lo = 0;")
lines.append(f"  int hi = {len(block_default_map)} - 1;")
lines.append("  while (lo <= hi) {")
lines.append("    int mid = (lo + hi) / 2;")
lines.append("    int cmp = std::strcmp(name.c_str(), kBlocks[mid].name);")
lines.append("    if (cmp == 0) return &kBlocks[mid];")
lines.append("    if (cmp < 0) hi = mid - 1;")
lines.append("    else lo = mid + 1;")
lines.append("  }")
lines.append("  return nullptr;")
lines.append("}")
lines.append("")
lines.append("inline const BakedBlockModel *FindVariant(const std::string &name) {")
lines.append("  int lo = 0;")
lines.append(f"  int hi = {len(extra_models)} - 1;")
lines.append("  while (lo <= hi) {")
lines.append("    int mid = (lo + hi) / 2;")
lines.append("    int cmp = std::strcmp(name.c_str(), kVariantModels[mid].name);")
lines.append("    if (cmp == 0) return &kVariantModels[mid];")
lines.append("    if (cmp < 0) hi = mid - 1;")
lines.append("    else lo = mid + 1;")
lines.append("  }")
lines.append("  return nullptr;")
lines.append("}")
lines.append("")
lines.append("}")

with open(OUT_HPP, "w", encoding="ascii") as f:
    f.write("\n".join(lines) + "\n")

print(f"Generated {OUT_HPP}: blocks={len(block_default_map)}, variants={len(extra_models)}, geoms={len(geom_list)}, cuboids={len(cuboid_pool)}, texKeys={len(tex_key_pool)}")
