"""Prepare or verify the public source-art kit using Blender's own FBX library.

Author preparation (private audit is never distributed):
  blender --background --factory-startup --python Tools/prepare_source_art_blender.py -- \
    --source-workspace <workspace> --audit <private-audit.json> \
    --model-contract <current-model-manifest.json>

Anyone can verify the published files:
  blender --background --factory-startup --python Tools/prepare_source_art_blender.py -- --check-only

This script imports Blender's installed parser/encoder; it does not contain or
distribute those libraries. It does not open, export or save a Blender scene.
Only a new SourceArt kit is written. Original FBX/PNG files remain read-only.
"""
from __future__ import annotations

import argparse
import array
import hashlib
import json
import math
import re
import shutil
import struct
import sys
from pathlib import Path

from io_scene_fbx import data_types, encode_bin, parse_fbx


REPO = Path(__file__).resolve().parents[1]
DEFAULT_OUT = REPO / "SourceArt"
ABSOLUTE_PATH = re.compile(rb"(?:[A-Za-z]:[\\/]|\\\\[^\\]+\\|file://)")
FILE_FIELDS = {b"FileName", b"Filename", b"RelativeFilename", b"RelativeFileName"}
ARRAY_TYPES = {
    data_types.INT32_ARRAY: "add_int32_array",
    data_types.INT64_ARRAY: "add_int64_array",
    data_types.FLOAT32_ARRAY: "add_float32_array",
    data_types.FLOAT64_ARRAY: "add_float64_array",
    data_types.BOOL_ARRAY: "add_bool_array",
    data_types.BYTE_ARRAY: "add_byte_array",
}
SCALAR_TYPES = {
    data_types.BOOL: "add_bool",
    data_types.CHAR: "add_char",
    data_types.INT8: "add_int8",
    data_types.INT16: "add_int16",
    data_types.INT32: "add_int32",
    data_types.INT64: "add_int64",
    data_types.FLOAT32: "add_float32",
    data_types.FLOAT64: "add_float64",
    data_types.BYTES: "add_bytes",
    data_types.STRING: "add_string",
}


def sha(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def inside_file(root: Path, relative: str) -> Path:
    candidate = (root / relative).resolve(strict=True)
    if not candidate.is_relative_to(root.resolve()) or not candidate.is_file():
        raise RuntimeError("Input must be a file inside the declared source root")
    if candidate.is_symlink():
        raise RuntimeError("File links are not allowed")
    return candidate


def read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def png_info(path: Path) -> dict:
    with path.open("rb") as stream:
        header = stream.read(33)
    if header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        raise RuntimeError("Expected a real PNG image")
    width, height, depth, colour_type, compression, filtering, interlace = struct.unpack(
        ">IIBBBBB", header[16:29])
    return {"size_px": [width, height], "bit_depth": depth,
            "png_colour_type": colour_type}


def walk(node):
    yield node
    for child in node.elems:
        yield from walk(child)


def prop_bytes(kind: int, value) -> bytes:
    if kind in ARRAY_TYPES:
        return value.tobytes()
    if kind in (data_types.STRING, data_types.BYTES, data_types.CHAR):
        return value
    formats = {data_types.BOOL: "<?", data_types.INT8: "<b", data_types.INT16: "<h",
               data_types.INT32: "<i", data_types.INT64: "<q",
               data_types.FLOAT32: "<f", data_types.FLOAT64: "<d"}
    return struct.pack(formats[kind], value)


def sanitised_fingerprint(node) -> str:
    """Hash every logical field after path removal, except writer footer IDs.

    Includes all geometry, normal/tangent, UV, vertex-colour, material indices,
    collision models, model transforms and connection data. Nothing is imported
    into Blender meshes or re-evaluated, so no geometry precision is lost.
    """
    h = hashlib.sha256()

    def add(elem):
        h.update(struct.pack("<I", len(elem.id)))
        h.update(elem.id)
        if elem.id in (b"FileId", b"CreationTime"):
            # Blender's writer normalises these two footer-support fields.
            h.update(b"writer-footer-field")
        else:
            h.update(struct.pack("<I", len(elem.props)))
            for kind, value in zip(elem.props_type, elem.props):
                payload = prop_bytes(kind, value)
                h.update(bytes([kind]))
                h.update(struct.pack("<Q", len(payload)))
                h.update(payload)
        h.update(struct.pack("<I", len(elem.elems)))
        for child in elem.elems:
            add(child)

    add(node)
    return h.hexdigest()


def geometry_summary(tree) -> dict:
    geometry = [n for n in walk(tree) if n.id == b"Geometry"]
    vertices = polygons = uv_layers = normals = tangents = colour_layers = 0
    models = []
    materials = []
    for node in geometry:
        for child in node.elems:
            if child.id == b"Vertices":
                vertices += len(child.props[0]) // 3
            elif child.id == b"PolygonVertexIndex":
                polygons += sum(v < 0 for v in child.props[0])
            elif child.id == b"LayerElementUV":
                uv_layers += 1
            elif child.id == b"LayerElementNormal":
                normals += 1
            elif child.id == b"LayerElementTangent":
                tangents += 1
            elif child.id == b"LayerElementColor":
                colour_layers += 1
    for node in walk(tree):
        if node.id in (b"Model", b"Material") and len(node.props) > 1:
            value = node.props[1].split(b"\x00\x01", 1)[0].decode("utf8", errors="replace")
            (models if node.id == b"Model" else materials).append(value)
    return {"geometry_object_count": len(geometry), "total_vertices_including_collision": vertices,
            "total_polygons_including_collision": polygons, "UV_layer_count": uv_layers,
            "normal_layer_count": normals, "tangent_layer_count": tangents,
            "vertex_colour_layer_count": colour_layers, "model_names": models,
            "material_names": materials}


def metric_contract(tree, main_name: str) -> dict:
    """Measure the current kit's FBX data in centimetres.

    The upstream exporter has already applied metre-to-centimetre scaling to
    vertices, so FBX UnitScaleFactor is 1 (one centimetre per stored unit).
    Its right-handed FBX frame converts to the plugin's Unreal frame by Y
    reflection. This is not another 100x mesh scale. Non-identity transforms
    are refused instead of silently reporting a misleading local bound.
    """
    settings = {}
    objects = {}
    connections = []
    for node in tree.elems:
        if node.id == b"GlobalSettings":
            for child in node.elems:
                if child.id == b"Properties70":
                    settings = {p.props[0]: p.props[4:] for p in child.elems}
        elif node.id == b"Objects":
            objects = {n.props[0]: n for n in node.elems if n.props}
        elif node.id == b"Connections":
            connections = [n.props for n in node.elems if n.id == b"C"]
    factor = float(settings[b"UnitScaleFactor"][0])
    axis = {name.decode("ascii"): settings[name][0] for name in
            (b"UpAxis", b"UpAxisSign", b"FrontAxis", b"FrontAxisSign", b"CoordAxis", b"CoordAxisSign")}
    expected_axis = {"UpAxis": 2, "UpAxisSign": 1, "FrontAxis": 1,
                     "FrontAxisSign": -1, "CoordAxis": 0, "CoordAxisSign": 1}
    if factor != 1.0 or axis != expected_axis:
        raise RuntimeError("Source kit FBX unit/axis contract changed; recheck engine conversion")
    main = None
    defaults = {b"Lcl Translation": [0, 0, 0], b"Lcl Rotation": [0, 0, 0],
                b"Lcl Scaling": [1, 1, 1], b"PreRotation": [0, 0, 0], b"PostRotation": [0, 0, 0],
                b"GeometricTranslation": [0, 0, 0], b"GeometricRotation": [0, 0, 0],
                b"GeometricScaling": [1, 1, 1]}
    for uid, node in objects.items():
        if node.id != b"Model":
            continue
        name = node.props[1].split(b"\x00\x01", 1)[0].decode("utf8")
        properties = {}
        for child in node.elems:
            if child.id == b"Properties70":
                properties = {p.props[0]: p.props[4:] for p in child.elems}
        for key, default in defaults.items():
            actual = properties.get(key, default)
            if len(actual) != 3 or any(abs(float(a) - b) > 1e-9 for a, b in zip(actual, default)):
                raise RuntimeError("Non-identity FBX model transform requires a new metric audit")
        if name == main_name:
            main = uid
    if main is None:
        raise RuntimeError("Main visual FBX model was not found")
    linked = [objects[c[1]] for c in connections if len(c) >= 3 and c[0] == b"OO"
              and c[2] == main and c[1] in objects and objects[c[1]].id == b"Geometry"]
    values = []
    for node in linked:
        for child in node.elems:
            if child.id == b"Vertices":
                values.extend(child.props[0])
    if not values:
        raise RuntimeError("Main visual FBX model has no linked vertex array")
    raw_min = [min(values[i::3]) * factor for i in range(3)]
    raw_max = [max(values[i::3]) * factor for i in range(3)]
    minimum = [raw_min[0], -raw_max[1], raw_min[2]]
    maximum = [raw_max[0], -raw_min[1], raw_max[2]]
    return {"centimetres_per_fbx_unit": factor, "global_axes": axis,
            "all_model_transforms_identity": True,
            "raw_fbx_bounds_cm": {"minimum_cm": raw_min, "maximum_cm": raw_max},
            "runtime_coordinate_conversion": "X=X, Y=-Y, Z=Z after ConvertScene; unit factor is 1",
            "runtime_bounds_cm": {"minimum_cm": minimum, "maximum_cm": maximum,
                                  "dimensions_cm": [b - a for a, b in zip(minimum, maximum)]}}


def verify_metric(row, tree):
    actual = metric_contract(tree, row["name"])
    for key in ("minimum_cm", "maximum_cm", "dimensions_cm"):
        declared = row["actual_bounds_cm"].get(key)
        measured = actual["runtime_bounds_cm"][key]
        if declared is None or len(declared) != 3 or any(abs(a - b) > .001 for a, b in zip(declared, measured)):
            raise RuntimeError("Declared cm bounds differ from measured FBX/runtime coordinates")
    if "pivot_m" in row or any(k.endswith("_m") for k in row["actual_bounds_cm"]):
        raise RuntimeError("Mixed metre/centimetre field names are not allowed in the public manifest")
    if row.get("pivot_cm") != [0, 0, 0]:
        raise RuntimeError("Current source kit requires the preserved zero model pivot")
    return actual


def clean_tree(tree):
    removed = []
    for node in walk(tree):
        if node.id == b"Content" and any(isinstance(p, bytes) and p for p in node.props):
            raise RuntimeError("Embedded content found; art provenance must be reviewed before publishing")
        for index, (kind, value) in enumerate(zip(node.props_type, node.props)):
            if kind != data_types.STRING:
                continue
            if node.id in FILE_FIELDS or ABSOLUTE_PATH.search(value):
                if value:
                    removed.append({"field": node.id.decode("ascii", errors="replace"),
                                    "property_index": index})
                node.props[index] = b""
    return removed


def encode_tree(node):
    result = encode_bin.FBXElem(node.id)
    for kind, value in zip(node.props_type, node.props):
        method = ARRAY_TYPES.get(kind) or SCALAR_TYPES.get(kind)
        if method is None:
            raise RuntimeError("Unsupported FBX property type")
        getattr(result, method)(value)
    result.elems = [encode_tree(child) for child in node.elems]
    return result


def check_paths(tree):
    return all(not ABSOLUTE_PATH.search(value)
               for node in walk(tree)
               for kind, value in zip(node.props_type, node.props)
               if kind == data_types.STRING)


def write_model(source: Path, dest: Path) -> dict:
    before_sha = sha(source)
    tree, version = parse_fbx.parse(str(source))
    original_stats = geometry_summary(tree)
    removed = clean_tree(tree)
    expected = sanitised_fingerprint(tree)
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists():
        raise RuntimeError("Preparation output already exists; choose a new output directory")
    encode_bin.write(str(dest), encode_tree(tree), version)
    reopened, actual_version = parse_fbx.parse(str(dest))
    actual_stats = geometry_summary(reopened)
    if actual_version != version or sanitised_fingerprint(reopened) != expected:
        raise RuntimeError("FBX logical fields changed during the metadata-only rewrite")
    if original_stats != actual_stats or not check_paths(reopened) or sha(source) != before_sha:
        raise RuntimeError("FBX preservation/path/source check failed")
    return {"fbx_version": version, "sha256": sha(dest), "original_sha256": before_sha,
            "logical_payload_sha256": expected, "metadata_fields_cleared": removed,
            "geometry_preserved": True, "no_absolute_paths_in_FBX_strings": True,
            "stats": actual_stats}


def texture_settings(name: str, kind: str) -> dict:
    if "Height" in name:
        return {"srgb": False, "compression": "TC_DISPLACEMENTMAP",
                "channel_contract": "R = micro weave height; editable source, not default displacement"}
    if "Normal" in name:
        return {"srgb": False, "compression": "TC_NORMALMAP", "flip_green_channel": False,
                "channel_contract": "RGB = DirectX tangent normal"}
    if "ORM" in name or "OcclusionRoughnessMetallic" in name:
        return {"srgb": False, "compression": "TC_MASKS",
                "channel_contract": "R = AO; G = Roughness; B = Metallic"}
    if "Roughness" in name:
        return {"srgb": False, "compression": "TC_MASKS", "channel_contract": "R = Roughness"}
    return {"srgb": True, "compression": "TC_BC7", "channel_contract": "RGB = colour"}


def verify(out: Path) -> dict:
    manifest = read_json(out / "manifest.json")
    for row in manifest["models"]:
        file = inside_file(out, row["file"])
        tree, version = parse_fbx.parse(str(file))
        if sha(file) != row["sha256"] or sanitised_fingerprint(tree) != row["logical_payload_sha256"]:
            raise RuntimeError("Published model differs from its manifest")
        if not check_paths(tree) or geometry_summary(tree) != row["stats"]:
            raise RuntimeError("Published model geometry/path verification failed")
        measured = verify_metric(row, tree)
        if measured != row["fbx_metric_contract"]:
            raise RuntimeError("Published FBX unit contract differs from the manifest")
    for row in manifest["textures"]:
        file = inside_file(out, row["file"])
        if sha(file) != row["sha256"] or png_info(file) != row["png"]:
            raise RuntimeError("Published texture differs from its manifest")
    expected = {row["file"] for row in manifest["models"] + manifest["textures"]}
    actual = {p.relative_to(out).as_posix() for p in out.rglob("*") if p.suffix.lower() in (".fbx", ".png")}
    if expected != actual or len(manifest["models"]) != 52 or len(manifest["textures"]) != 31:
        raise RuntimeError("SourceArt inventory differs from the declared public kit")
    return {"success": True, "models": 52, "textures": 31,
            "FBX_data_preservation_and_string_paths": True, "texture_hashes": True,
            "all_52_FBX_units_and_runtime_cm_bounds_verified": True,
            "scope": "Blender FBX logical data and PNG files, not Unreal rendering or art acceptance"}


def prepare(args):
    root = args.source_workspace.resolve(strict=True)
    audit = read_json(args.audit.resolve(strict=True))
    contract = read_json(args.model_contract.resolve(strict=True))
    contract_models = {r["name"]: r for r in contract["assets"]}
    out = args.output.resolve()
    if out.exists() and any(out.iterdir()):
        raise RuntimeError("Choose a new empty SourceArt output directory; no old delivery is overwritten")
    if out.is_relative_to(root / "Art"):
        raise RuntimeError("Output must not overwrite the original art source tree")
    out.mkdir(parents=True, exist_ok=True)
    manifest = {"format_version": 1, "plugin_version": "0.4.7-assets.1",
                "public_source_art_only": True, "models": [], "textures": [],
                "original_assets_modified": False,
                "geometry_method": "Blender FBX logical-data rewrite, only source path strings cleared",
                "import_materials": False, "import_textures": False,
                "axes": "Runtime +X right/-Y exterior/+Z up; right-handed FBX uses Y reflection during ConvertScene",
                "nominal_room_cell_cm": [300, 300, 300],
                "units": "Published FBX vertices are centimetres and UnitScaleFactor=1; DCC authoring contracts were metres",
                "AI_disclosure": "One current red/gold canvas BaseColor is image_gen output; other included maps are authored or technical bakes",
                "wood_replacement": "Six independent project-authored wood maps replace private Painter starter outputs"}
    for item in audit["include_geometry"]:
        source = inside_file(root, item["source_relative"])
        if sha(source) != item["sha256"]:
            raise RuntimeError("Audited source model changed")
        name = source.stem
        record = {"name": name, "file": "Models/" + source.name,
                  "original_runtime_asset": item["original_runtime_asset"],
                  "style_property": item["style_property"],
                  "material_slots": item.get("materials", []),
                  "origin": "Project-authored Blender geometry"}
        native = contract_models[name]
        record["actual_bounds_cm"] = {k.removesuffix("_m") + "_cm": [round(v * 100, 6) for v in vals]
                                     for k, vals in native["actual_bounds"].items()}
        record["pivot_cm"] = [round(v * 100, 6) for v in native.get("pivot_m", [0, 0, 0])]
        for key in ("nominal_reference_cm", "ucx_names", "expected_convex_collision_count", "uv_layers", "stair_contract"):
            if key in native:
                record[key] = native[key]
        record.update(write_model(source, out / record["file"]))
        tree, _ = parse_fbx.parse(str(out / record["file"]))
        record["fbx_metric_contract"] = verify_metric(record, tree)
        manifest["models"].append(record)
    source_maps = [(r, False) for r in audit["include_native_authored_texture_sources"]]
    source_maps += [(r, True) for r in audit["include_independently_authored_wood_replacements"]]
    for item, wood in source_maps:
        source = inside_file(root, item["source_relative"])
        if sha(source) != item["sha256"]:
            raise RuntimeError("Audited source texture changed")
        file = ("Textures/AuthoredWood/" if wood else "Textures/Original/") + source.name
        target = out / file
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists():
            raise RuntimeError("Public source texture name collision")
        shutil.copy2(source, target)
        if sha(source) != item["sha256"] or sha(target) != item["sha256"]:
            raise RuntimeError("Source texture changed during verified copy")
        runtime = ("/DesertBuildingLab/Art07/Textures/" + item["replacement_runtime_name"]
                   if wood else item["original_runtime_asset"])
        row = {"file": file, "sha256": item["sha256"], "bytes": target.stat().st_size,
               "original_runtime_asset": runtime, "replaces_private_starter_output": wood,
               "origin": "Project-authored wood" if wood else item["origin"],
               "kind": item["kind"], "png": png_info(target),
               **texture_settings(source.name, item["kind"]), "address_x": "TA_WRAP", "address_y": "TA_WRAP"}
        manifest["textures"].append(row)
    (out / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf8")
    result = verify(out)
    (out / "verification.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf8")
    return result


def upgrade_unit_metadata(out: Path) -> dict:
    """Correct an early public manifest's labels without changing any FBX/PNG."""
    manifest = read_json(out / "manifest.json")
    before = {r["file"]: sha(inside_file(out, r["file"]))
              for r in manifest["models"] + manifest["textures"]}
    for row in manifest["models"]:
        # Earlier values had already been multiplied by 100; only names change.
        row["actual_bounds_cm"] = {k.removesuffix("_m") + "_cm" if k.endswith("_m") else k: vals
                                   for k, vals in row["actual_bounds_cm"].items()}
        if "pivot_m" in row:
            row["pivot_cm"] = [v * 100 for v in row.pop("pivot_m")]
        else:
            row.setdefault("pivot_cm", [0, 0, 0])
        tree, _ = parse_fbx.parse(str(inside_file(out, row["file"])))
        row["fbx_metric_contract"] = verify_metric(row, tree)
    manifest["axes"] = "Runtime +X right/-Y exterior/+Z up; right-handed FBX uses Y reflection during ConvertScene"
    manifest["units"] = "Published FBX vertices are centimetres and UnitScaleFactor=1; DCC authoring contracts were metres"
    manifest["unit_metadata_revision"] = 2
    (out / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf8")
    result = verify(out)
    if any(sha(inside_file(out, file)) != value for file, value in before.items()):
        raise RuntimeError("Unit metadata update must not modify any FBX/PNG")
    result["unit_metadata_update_left_FBX_and_PNG_bytes_unchanged"] = True
    (out / "verification.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf8")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--source-workspace", type=Path)
    parser.add_argument("--audit", type=Path)
    parser.add_argument("--model-contract", type=Path)
    parser.add_argument("--check-only", action="store_true")
    parser.add_argument("--upgrade-unit-metadata", action="store_true")
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    if args.upgrade_unit_metadata:
        result = upgrade_unit_metadata(args.output.resolve(strict=True))
    elif args.check_only:
        result = verify(args.output.resolve(strict=True))
    else:
        if not all((args.source_workspace, args.audit, args.model_contract)):
            parser.error("Preparation requires --source-workspace, --audit and --model-contract")
        result = prepare(args)
    print("DESERT_PUBLIC_SOURCE_ART " + json.dumps(result, ensure_ascii=False))


if __name__ == "__main__":
    main()
