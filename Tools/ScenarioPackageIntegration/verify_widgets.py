import datetime
import hashlib
import json
from pathlib import Path
import unreal

project = Path(unreal.Paths.project_dir()).resolve()
root = project.parent
output = root / ".forgemind-tmp/scenario-package-integration"
preflight = json.loads((output / "preflight.json").read_text(encoding="utf-8"))
assert preflight["status"] == "pass"
assert Path(preflight["checkout"]) == root
package = preflight["installedPackage"]
api = unreal.FlyingInstalledPackageLibrary

def select_package(data_root, relative):
    diagnostic = api.select_installed_package(data_root, relative, package["manifestSha256"])
    assert diagnostic is not None, "Installed package selection failed"
    return diagnostic

select_package(package["dataRoot"], package["relativePackage"])
selection = unreal.new_object(unreal.FlyingScenarioSelectionWidget)
editor = unreal.new_object(unreal.FlyingScenarioEditorWidget)
selection.refresh_pilot_locations()
editor.refresh_package_locations()
locations = selection.get_editor_property("pilot_locations")
editor_locations = editor.get_editor_property("package_locations")
assert len(locations) > 0
assert [str(entry.location_id) for entry in locations] == [str(entry.location_id) for entry in editor_locations]
assert all(entry.aerodrome_id and entry.runway_id for entry in locations)
records = []
for location in locations:
    identifier = location.location_id
    for mode in [unreal.FlyingScenarioStartMode.COLD_AND_DARK, unreal.FlyingScenarioStartMode.READY_TO_TAXI]:
        expected = location.ground_eligible
        assert selection.select_scenario(identifier, mode) == expected
        assert editor.select_package_location(identifier, mode) == expected
        records.append(dict(location=str(identifier), mode=str(mode), eligible=expected,
                            selectionDiagnostic=selection.get_editor_property("last_status"),
                            editorDiagnostic=editor.get_editor_property("last_status")))
    assert not selection.select_scenario(identifier, unreal.FlyingScenarioStartMode.AIRBORNE)
    assert "Terrain sampling" in selection.get_editor_property("last_status")
    assert not editor.select_package_location(identifier, unreal.FlyingScenarioStartMode.AIRBORNE)
    assert "Terrain sampling" in editor.get_editor_property("last_status")

saved_selection = editor.get_editor_property("edited_scenario")
saved_selection.airport_selection.location_id = locations[0].location_id
saved_selection.airport_selection.start_mode = unreal.FlyingScenarioStartMode.AIRBORNE
editor.set_editor_property("edited_scenario", saved_selection)
editor.set_editor_property("scenario_save_path", str(output / "saved-scenario.json"))
assert editor.save_scenario(), editor.get_editor_property("last_status")
saved_path = output / "saved-scenario.json"
saved = json.loads(saved_path.read_text(encoding="utf-8-sig"))
saved["packageManifestSha256"] = package["manifestSha256"]
saved_path.write_text(json.dumps(saved, indent=2) + "\n", encoding="utf-8")
assert not editor.load_scenario()
assert "Terrain sampling" in editor.get_editor_property("last_status")
saved = json.loads((output / "saved-scenario.json").read_text(encoding="utf-8-sig"))
assert saved["packageManifestSha256"] == package["manifestSha256"]
select_package(str(Path(package["dataRoot"]).parent), "managed/" + package["relativePackage"])
assert not selection.start_selected_scenario(None)
assert "Stale" in selection.get_editor_property("last_status")
selection.refresh_pilot_locations()
editor.refresh_package_locations()
assert selection.get_editor_property("selected_scenario").package_generation == 0
assert editor.get_editor_property("edited_scenario").airport_selection.package_generation == 0
world = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level("/Game/Flying/Maps/FlyingStartup")
assert world
module_sha256 = hashlib.sha256((project / "Binaries/Win64/UnrealEditor-FlyingPresentation.dll").read_bytes()).hexdigest()
report = dict(schemaVersion="flying.scenario-package-widget-integration.v1", status="pass",
              recordedAtUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
              engine=unreal.SystemLibrary.get_engine_version(), package=package,
              editorModuleSha256=module_sha256, buildId="editor-scenario-package-" + module_sha256[:16],
              startupAsset="/Game/Flying/Maps/FlyingStartup", canonicalBaselineBuildId=preflight["canonicalBaselineBuildId"],
              groundModeResults=records, airborneCatalogTerrainRejection=True,
              saveLoadRejectedWithoutTerrainProvider=True,
              selectionChangeInvalidation=True, configuredRootRelocation=True,
              limitations=["No world rendering or installed Shipping acceptance performed",
                           "All airborne starts require a package-aware terrain elevation provider; no provider is registered by this widget-only harness"])
(output / "widget-integration.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
unreal.log("Production-package widget integration passed")
