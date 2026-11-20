import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const directory = process.argv[2] ?? '.forgemind-tmp/scenario-package-integration';
const read = name => JSON.parse(fs.readFileSync(path.join(directory, name), 'utf8'));
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const preflight = read('preflight.json');
const widgets = read('widget-integration.json');
const identity = read('checkpoint.json');
assert.equal(preflight.status, 'pass');
assert.equal(widgets.status, 'pass');
assert.equal(identity.status, 'pass');
assert.equal(identity.classification, 'automated-scenario');
assert.equal(identity.buildId, widgets.buildId);
assert.equal(identity.editorModuleSha256, widgets.editorModuleSha256);
assert.equal(preflight.canonicalBaselineBuildId, widgets.canonicalBaselineBuildId);
assert.equal(widgets.package.manifestSha256, preflight.installedPackage.manifestSha256);
for (const key of ['airborneCatalogTerrainRejection', 'saveLoadRejectedWithoutTerrainProvider', 'selectionChangeInvalidation', 'configuredRootRelocation']) assert.equal(widgets[key], true);
assert(widgets.groundModeResults.length > 0);
for (const record of widgets.groundModeResults) {
  if (!record.eligible) assert(record.selectionDiagnostic.includes('unverified') && record.editorDiagnostic.includes('unverified'));
}
for (const asset of preflight.startupAssets) assert.equal(hash(asset.path), asset.sha256);
assert(identity.sources.length > 0);
for (const source of identity.sources) {
  assert.equal(source.hashMode, 'utf8-lf');
  const text = fs.readFileSync(source.path, 'utf8').replace(/\r\n/g, '\n');
  assert.equal(crypto.createHash('sha256').update(text).digest('hex'), source.sha256, 'Changed integration input: ' + source.path);
}
assert(identity.artifacts.length >= 4);
for (const artifact of identity.artifacts) assert.equal(hash(path.join(directory, artifact.path)), artifact.sha256, 'Changed proof artifact: ' + artifact.path);
assert.equal(hash(path.join(directory, 'installed-package/install-manifest.json')), widgets.package.manifestSha256);
const manifest = read('installed-package/install-manifest.json');
for (const [relative, expected] of Object.entries(manifest.files)) {
  assert(!path.isAbsolute(relative) && !relative.split(/[\\/]/).includes('..'));
  assert.equal(hash(path.join(directory, 'installed-package', relative)), expected, relative);
}
const junit = fs.readFileSync(path.join(directory, 'native-regressions.xml'), 'utf8');
assert(junit.includes('flying.core_sim.scenario_start'));
assert(!/<(?:failure|error|skipped)\b/.test(junit));
assert(/<testcase\b/.test(junit));
assert(!/\b(?:failures|errors)="[1-9][0-9]*"/.test(junit));
console.log('Current-source production-package widget, startup and native regression evidence passed');
