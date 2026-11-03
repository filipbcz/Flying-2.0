"""Materialize the Editor entry point for the existing authorized runtime builder.

Run only inside the canonical Editor via run_unreal_authoring. This saves map and
Blueprint packages; regional content remains owned by FlyingPilotGameMode.
A separate invocation with -FlyingBaselineVerify only loads and checks packages.
"""
import datetime
import hashlib
import json
from pathlib import Path

import unreal

PROJECT = Path(unreal.Paths.project_dir()).resolve()
REPO = PROJECT.parent
EVIDENCE = REPO / "docs/validation/editor-baseline"
MAP = "/Game/Flying/Maps/FlyingStartup"
GAME_MODE = "/Game/Flying/Blueprints/BP_FlyingPilotGameMode"
MATERIAL = "/Game/Flying/Materials/M_OfflineVertexColor"
VERIFY = "-FlyingBaselineVerify" in unreal.SystemLibrary.get_command_line()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    preflight = json.loads((EVIDENCE / "preflight.json").read_text(encoding="utf-8"))
    require(PROJECT == Path(preflight["identity"]["checkout"]) / "unreal",
            "Editor is not using the canonical checkout")
    require(Path(unreal.Paths.get_project_file_path()).resolve() == PROJECT / "Flying.uproject",
            "Unexpected project descriptor")
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assets = unreal.EditorAssetLibrary
    native_game_mode = unreal.load_class(None, "/Script/FlyingPresentation.FlyingPilotGameMode")
    require(native_game_mode is not None, "Existing runtime content builder is unavailable")

    if not VERIFY:
        if not assets.does_asset_exist(MATERIAL):
            material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                "M_OfflineVertexColor", "/Game/Flying/Materials", unreal.Material, unreal.MaterialFactoryNew())
            require(material is not None, "Terrain material creation failed")
            vertex_color = unreal.MaterialEditingLibrary.create_material_expression(
                material, unreal.MaterialExpressionVertexColor, -200, 0)
            require(unreal.MaterialEditingLibrary.connect_material_property(
                vertex_color, "", unreal.MaterialProperty.MP_BASE_COLOR), "Vertex color binding failed")
            unreal.MaterialEditingLibrary.recompile_material(material)
        require(assets.save_asset(MATERIAL, only_if_is_dirty=False), "Terrain material save failed")
        if not assets.does_asset_exist(GAME_MODE):
            factory = unreal.BlueprintFactory()
            factory.set_editor_property("parent_class", native_game_mode)
            blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                "BP_FlyingPilotGameMode", "/Game/Flying/Blueprints", unreal.Blueprint, factory)
            require(blueprint is not None, "Failed to materialize runtime builder Blueprint")
        require(assets.save_asset(GAME_MODE, only_if_is_dirty=False), "Blueprint save failed")
        if assets.does_asset_exist(MAP):
            require(levels.load_level(MAP), "Existing startup map cannot be loaded")
        else:
            require(levels.new_level(MAP), "Startup map creation failed")
            actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
            georeference = actors.spawn_actor_from_class(unreal.CesiumGeoreference, unreal.Vector())
            require(georeference is not None, "Cesium georeference creation failed")
            georeference.set_actor_label("Flying Offline Georeference")
            settings = unreal.get_default_object(unreal.FlyingPresentationSettings)
            georeference.set_origin_longitude_latitude_height(unreal.Vector(
                settings.get_editor_property("pilot_origin_longitude_degrees"),
                settings.get_editor_property("pilot_origin_latitude_degrees"),
                settings.get_editor_property("pilot_origin_height_meters")))
            sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1000),
                                                unreal.Rotator(-35, -30, 0))
            sun.set_actor_label("Flying Startup Sun")
            sun.light_component.set_editor_property("atmosphere_sun_light", True)
            actors.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector())
            actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 500))
            actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 200))
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        world.get_world_settings().set_editor_property(
            "default_game_mode", assets.load_blueprint_class(GAME_MODE))
        require(levels.save_current_level(), "Startup map save failed")

    require(levels.load_level(MAP), "Saved startup map failed to open")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    require(world.get_path_name() == MAP + ".FlyingStartup", "Wrong world opened")
    blueprint = unreal.load_asset(GAME_MODE)
    require(blueprint is not None, "Saved Blueprint failed to load")
    mode = world.get_world_settings().get_editor_property("default_game_mode")
    require(mode == assets.load_blueprint_class(GAME_MODE), "Map has wrong content builder")
    require(unreal.MathLibrary.class_is_child_of(mode, native_game_mode),
            "Blueprint does not delegate to the authorized runtime content builder")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    georeferences = [a for a in actors if isinstance(a, unreal.CesiumGeoreference)]
    require(len(georeferences) == 1, "Startup requires exactly one Cesium georeference")
    require(not any(isinstance(a, unreal.Cesium3DTileset) for a in actors),
            "Baseline must not introduce a remote tileset")
    require(unreal.load_asset(MATERIAL) is not None, "Terrain material failed to load")
    files = [PROJECT / "Content/Flying/Materials/M_OfflineVertexColor.uasset",
             PROJECT / "Content/Flying/Maps/FlyingStartup.umap",
             PROJECT / "Content/Flying/Blueprints/BP_FlyingPilotGameMode.uasset"]
    for asset in files:
        require(asset.is_file() and asset.stat().st_size > 1024, "Unreal package is missing or empty")
        require(asset.read_bytes()[:4] == bytes.fromhex("c1832a9e"), "Invalid Unreal package signature")
    record = {
        "schemaVersion": "flying.editor-baseline-open.v1",
        "status": "pass",
        "mode": "verify" if VERIFY else "materialize",
        "recordedAtUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "identity": preflight["identity"],
        "engineVersion": unreal.SystemLibrary.get_engine_version(),
        "world": world.get_path_name(),
        "gameMode": mode.get_path_name(),
        "nativeContentBuilder": native_game_mode.get_path_name(),
        "georeferenceCount": len(georeferences),
        "actors": sorted(a.get_class().get_path_name() for a in actors),
        "artifacts": [{"path": str(p.relative_to(REPO)).replace("\\", "/"),
                       "sha256": digest(p), "sizeBytes": p.stat().st_size} for p in files],
        "scope": "Editor baseline only; regional GIS, Shipping and performance acceptance are separate gates",
    }
    report = EVIDENCE / ("editor-open.json" if VERIFY else "materialization.json")
    report.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    unreal.log("FLYING_EDITOR_BASELINE_PASS " + record["mode"] + " " + world.get_path_name())


try:
    main()
except Exception:
    report = EVIDENCE / ("editor-open.json" if VERIFY else "materialization.json")
    report.unlink(missing_ok=True)
    raise
