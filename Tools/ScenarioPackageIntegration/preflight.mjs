import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
const root = process.cwd();
const output = path.resolve(process.argv[2] ?? '.forgemind-tmp/scenario-package-integration');
const engine = process.argv[3];
const cesium = process.argv[4];
assert(engine && cesium, 'Pass the runner-probed Unreal root and Cesium manifest path');
const read = relative => JSON.parse(fs.readFileSync(path.resolve(root, relative), 'utf8'));
const digest = relative => crypto.createHash('sha256').update(fs.readFileSync(path.resolve(root, relative))).digest('hex');
const baseline = read('docs/validation/editor-baseline/acceptance.json');
assert.equal(baseline.status, 'pass');
const artifacts = baseline.artifacts.filter(record => /\.(umap|uasset)$/.test(record.path));
assert(artifacts.some(record => record.path.endsWith('FlyingStartup.umap')));
for (const artifact of artifacts) assert.equal(digest(artifact.path), artifact.sha256, 'Canonical startup asset changed: ' + artifact.path);
const version = read(path.join(engine, 'Engine/Build/Build.version'));
assert.equal(version.MajorVersion, 5);
assert(version.MinorVersion >= 8);
const plugin = read(cesium);
assert.equal(plugin.VersionName, '2.29.1');
const project = read('unreal/Flying.uproject');
assert(project.Plugins.some(entry => entry.Name === 'CesiumForUnreal' && entry.Enabled));
const installed = '.forgemind-tmp/managed/regional-airport-navigation';
const manifest = read(installed + '/install-manifest.json');
assert.equal(manifest.runtimeNetworkRequired, false);
const report = { schemaVersion: 'flying.scenario-package-preflight.v1', status: 'pass',
  recordedAtUtc: new Date().toISOString(), checkout: root,
  canonicalBaselineBuildId: baseline.buildId, startupAssets: artifacts,
  engine: version, cesium: plugin.VersionName,
  installedPackage: { dataRoot: path.resolve('.forgemind-tmp/managed'), relativePackage: path.basename(installed),
    id: manifest.regionId, version: manifest.version, manifestSha256: digest(installed + '/install-manifest.json') },
  scope: 'Current-session compatibility and canonical startup-asset integrity, not Shipping acceptance' };
fs.mkdirSync(output, { recursive: true });
fs.writeFileSync(path.join(output, 'preflight.json'), JSON.stringify(report, null, 2) + '\n');
console.log('Current-session canonical startup and package preflight passed');
