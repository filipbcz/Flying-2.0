import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

const directory = process.argv[2] ?? '.forgemind-tmp/scenario-package-integration';
const read = name => JSON.parse(fs.readFileSync(path.join(directory, name), 'utf8'));
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const widgets = read('widget-integration.json');
assert.equal(widgets.status, 'pass');
const modulePath = 'unreal/Binaries/Win64/UnrealEditor-FlyingPresentation.dll';
const moduleSha256 = hash(fs.readFileSync(modulePath));
assert.equal(widgets.editorModuleSha256, moduleSha256, 'Widget proof must use the current compiled module');
function collect(location) {
  if (fs.statSync(location).isDirectory()) {
    return fs.readdirSync(location).flatMap(name => collect(location + '/' + name));
  }
  return [location];
}
const files = [
  'core_sim', 'geo_terrain', 'unreal/Source/FlyingCoreSimBridge', 'unreal/Source/FlyingPresentation',
  'Tools/ScenarioPackageIntegration', 'Tools/UnrealBaseline/validate_native.cmake',
  'Tests/core_sim/scenario_start_test.cpp', 'tests/CMakeLists.txt', 'CMakeLists.txt',
  'CMakePresets.json', 'cmake', 'unreal/Config/DefaultGame.ini', 'unreal/Flying.uproject',
  'docs/scenario-installed-packages.md'].flatMap(collect);
const sources = [...new Set(files)].sort().map(file => ({ path: file, hashMode: 'utf8-lf',
  sha256: hash(fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n')) }));
assert(sources.some(source => source.path.endsWith('record_checkpoint.mjs')));
const artifacts = ['preflight.json', 'widget-integration.json', 'native-regressions.xml', 'saved-scenario.json']
  .map(file => ({ path: file, sha256: hash(fs.readFileSync(path.join(directory, file))) }));
const checkpoint = {
  schemaVersion: 'flying.scenario-package-checkpoint.v1', status: 'pass',
  recordedAtUtc: new Date().toISOString(), buildId: widgets.buildId,
  classification: 'automated-scenario', scenario: 'Production installed regional package widgets and save/load',
  settings: { worldRendering: false, offlineManagedInput: true, NullRHI: true, shippingAcceptance: false },
  sources, artifacts, editorModuleSha256: moduleSha256
};
fs.writeFileSync(path.join(directory, 'checkpoint.json'), JSON.stringify(checkpoint, null, 2) + '\n');
console.log('Recorded current-source scenario package checkpoint');
