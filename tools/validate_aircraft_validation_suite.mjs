#!/usr/bin/env node

import {readFileSync, statSync} from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const REQUIRED_CATEGORIES = new Set([
  'stall_speeds',
  'maximum_and_cruise_speeds',
  'climb_rate',
  'service_ceiling',
  'glide',
  'sink_rate',
  'takeoff_distance',
  'landing_distance',
  'engine_rpm_power_fuel_temperature_points',
  'stability_modes',
  'control_step_responses',
  'coordinated_turns',
  'ground_effect',
  'crosswind',
  'tire_slip',
  'braking',
]);

const RESULT_STATUSES = new Set([
  'pass',
  'fail',
  'approved_exclusion_missing_credible_data',
]);

const GRAPH_COLUMNS = ['metric', 'reference', 'actual', 'deviation', 'tolerance', 'status'];

function fail(message) {
  throw new Error(message);
}

function requireCondition(condition, message) {
  if (!condition) {
    fail(message);
  }
}

function loadJson(filePath) {
  return JSON.parse(readFileSync(filePath, 'utf8'));
}

function stableValue(value) {
  if (Array.isArray(value)) {
    return value.map(stableValue);
  }
  if (value && typeof value === 'object') {
    return Object.fromEntries(
      Object.entries(value)
        .sort(([left], [right]) => left.localeCompare(right))
        .map(([key, entry]) => [key, stableValue(entry)]),
    );
  }
  return value;
}

function valuesEqual(left, right) {
  return JSON.stringify(stableValue(left)) === JSON.stringify(stableValue(right));
}

function validateReferenceRegistry(registry) {
  requireCondition(
    registry.schemaVersion === 'flying.aircraft_reference_registry.v1',
    'reference registry schema version mismatch',
  );
  requireCondition(
    registry.aircraftModel === 'flying_trainer_one',
    'reference registry must target flying_trainer_one',
  );
  requireCondition(
    registry.referenceAircraftIdentity?.faithfulTypeClaimPermitted === false,
    'reference registry must prohibit faithful type claims',
  );

  const sources = new Map((registry.sources ?? []).map((source) => [source.id, source]));
  requireCondition(sources.size > 0, 'reference registry must define sources');

  const exclusion = sources.get('missing-poh-afm-flight-test-cfd-windtunnel');
  requireCondition(
    exclusion?.approvedExclusion?.status === 'approved',
    'missing credible data source must carry an approved exclusion',
  );
  return sources;
}

function validateScenarios(scenariosDocument, sources) {
  requireCondition(
    scenariosDocument.schemaVersion === 'flying.aircraft_validation_scenarios.v1',
    'scenario schema version mismatch',
  );
  const scenarios = scenariosDocument.scenarios ?? [];
  requireCondition(scenarios.length > 0, 'scenario definitions must not be empty');

  const categories = new Set(scenarios.map((scenario) => scenario.category));
  const missingCategories = [...REQUIRED_CATEGORIES].filter((category) => !categories.has(category));
  requireCondition(
    missingCategories.length === 0,
    `scenario definitions missing categories: ${missingCategories.join(', ')}`,
  );

  const scenarioIds = new Set();
  for (const scenario of scenarios) {
    requireCondition(typeof scenario.id === 'string' && scenario.id.length > 0,
      'each scenario must have an id');
    requireCondition(!scenarioIds.has(scenario.id), `duplicate scenario id: ${scenario.id}`);
    scenarioIds.add(scenario.id);
    requireCondition(sources.has(scenario.referenceSource),
      `${scenario.id} references an unknown source`);
    requireCondition(scenario.inputs && typeof scenario.inputs === 'object' && !Array.isArray(scenario.inputs),
      `${scenario.id} must store inputs`);
    requireCondition(Array.isArray(scenario.outputs) && scenario.outputs.length > 0,
      `${scenario.id} must define outputs`);
    requireCondition(scenario.tolerance && typeof scenario.tolerance === 'object',
      `${scenario.id} must define tolerance metadata`);

    const source = sources.get(scenario.referenceSource);
    if (source.credibleForTypeValidation === true) {
      requireCondition(
        scenario.tolerance.status !== 'not_defined_missing_credible_data',
        `${scenario.id} has credible data and must define numeric tolerance`,
      );
    } else {
      requireCondition(
        scenario.tolerance.status === 'not_defined_missing_credible_data',
        `${scenario.id} must not invent tolerance without credible data`,
      );
    }
  }
  return new Map(scenarios.map((scenario) => [scenario.id, scenario]));
}

function resultIsMandatoryReferenceBacked(result, scenario, source) {
  return scenario?.mandatoryWhenReferenceExists === true
    && source?.credibleForTypeValidation === true
    && result.tolerance?.status !== 'not_defined_missing_credible_data';
}

function resultIsReleaseBlockingFailure(result, scenario, source) {
  return resultIsMandatoryReferenceBacked(result, scenario, source)
    && result.resultStatus === 'fail';
}

function parseCsvRows(text) {
  const lines = text.trim().split(/\r?\n/);
  return {
    columns: lines[0]?.split(',') ?? [],
    rows: lines.slice(1).map((line) => line.split(',')),
  };
}

function validateGraph(root, result, scenario) {
  const graphPath = path.join(root, result.deviationGraph);
  requireCondition(statSync(graphPath).isFile(), `missing deviation graph: ${graphPath}`);
  const graph = parseCsvRows(readFileSync(graphPath, 'utf8'));
  requireCondition(
    JSON.stringify(graph.columns) === JSON.stringify(GRAPH_COLUMNS),
    `${graphPath} must use graph columns ${GRAPH_COLUMNS.join(',')}`,
  );
  requireCondition(graph.rows.length > 0, `${graphPath} must contain at least one graph row`);
  const expectedMetrics = new Set(scenario.outputs);
  const graphMetrics = new Set();
  for (const row of graph.rows) {
    requireCondition(row.length === GRAPH_COLUMNS.length, `${graphPath} contains malformed graph row`);
    requireCondition(expectedMetrics.has(row[0]), `${graphPath} contains undeclared metric ${row[0]}`);
    requireCondition(!graphMetrics.has(row[0]), `${graphPath} contains duplicate metric ${row[0]}`);
    graphMetrics.add(row[0]);
    requireCondition(RESULT_STATUSES.has(row[5]), `${graphPath} contains invalid graph status`);
  }
  const missingMetrics = [...expectedMetrics].filter((metric) => !graphMetrics.has(metric));
  requireCondition(
    missingMetrics.length === 0,
    `${graphPath} missing declared output metrics: ${missingMetrics.join(', ')}`,
  );
}

function validateResults(root, resultsDocument, scenarios, sources) {
  requireCondition(
    resultsDocument.schemaVersion === 'flying.aircraft_validation_results.v1',
    'results schema version mismatch',
  );
  requireCondition(
    resultsDocument.faithfulTypeClaim === false,
    'aircraft model must not be labeled faithful while exclusions remain',
  );

  const results = resultsDocument.results ?? [];
  requireCondition(results.length > 0, 'validation results must not be empty');
  const resultIds = new Set(results.map((result) => result.scenarioId));
  requireCondition(
    resultIds.size === scenarios.size && [...scenarios.keys()].every((id) => resultIds.has(id)),
    'validation results must cover exactly the defined scenarios',
  );

  const blockingFailures = [];
  const mandatoryFailures = [];
  const unresolvedExclusions = [];
  for (const result of results) {
    for (const field of [
      'inputs',
      'outputs',
      'referenceSource',
      'tolerance',
      'resultStatus',
      'stateHash',
      'deviationGraph',
      'releaseBlocking',
    ]) {
      requireCondition(Object.hasOwn(result, field), `${result.scenarioId} missing result field ${field}`);
    }
    requireCondition(result.inputs && typeof result.inputs === 'object' && !Array.isArray(result.inputs),
      `${result.scenarioId} inputs must be an object`);
    requireCondition(result.outputs && typeof result.outputs === 'object' && !Array.isArray(result.outputs),
      `${result.scenarioId} outputs must be an object`);
    requireCondition(sources.has(result.referenceSource), `${result.scenarioId} references unknown source`);
    requireCondition(RESULT_STATUSES.has(result.resultStatus), `${result.scenarioId} has invalid status`);
    requireCondition(typeof result.stateHash === 'string' && result.stateHash.startsWith('0x'),
      `${result.scenarioId} must store a hex state hash`);

    const scenario = scenarios.get(result.scenarioId);
    const source = sources.get(result.referenceSource);
    const expectedReleaseBlocking = resultIsMandatoryReferenceBacked(result, scenario, source);
    requireCondition(valuesEqual(result.inputs, scenario.inputs),
      `${result.scenarioId} result inputs drifted from scenario inputs`);
    requireCondition(valuesEqual(result.tolerance, scenario.tolerance),
      `${result.scenarioId} result tolerance drifted from scenario tolerance`);
    requireCondition(result.referenceSource === scenario.referenceSource,
      `${result.scenarioId} result reference source drifted from scenario source`);
    requireCondition(valuesEqual(Object.keys(result.outputs).sort(), [...scenario.outputs].sort()),
      `${result.scenarioId} result outputs drifted from scenario outputs`);
    requireCondition(result.releaseBlocking === expectedReleaseBlocking,
      `${result.scenarioId} releaseBlocking must be derived from mandatory credible reference data`);
    validateGraph(root, result, scenario);

    if (resultIsReleaseBlockingFailure(result, scenario, source)) {
      blockingFailures.push(result.scenarioId);
    }
    if (result.resultStatus === 'fail') {
      mandatoryFailures.push(result.scenarioId);
    }
    if (result.resultStatus === 'approved_exclusion_missing_credible_data') {
      requireCondition(
        source?.approvedExclusion?.status === 'approved',
        `${result.scenarioId} exclusion lacks approval`,
      );
      unresolvedExclusions.push(result.scenarioId);
    }
  }

  requireCondition(
    blockingFailures.length === 0,
    `release-blocking reference-backed failures: ${blockingFailures.join(', ')}`,
  );
  if (resultsDocument.faithfulTypeClaim) {
    requireCondition(
      mandatoryFailures.length === 0 && unresolvedExclusions.length === 0,
      'faithful type claim requires all mandatory tests to pass without exclusions',
    );
  }
}

function validateReleaseBlockFixture(baseDocument, sources) {
  const fixture = structuredClone(baseDocument);
  const first = fixture.results[0];
  const scenario = {
    mandatoryWhenReferenceExists: true,
    outputs: Object.keys(first.outputs),
  };
  first.referenceSource = 'fixture-credible-source';
  first.tolerance = {metric: 'fixture', absolute: 0.1, unit: 'mps'};
  first.resultStatus = 'fail';
  const fixtureSources = new Map(sources);
  fixtureSources.set('fixture-credible-source', {
    id: 'fixture-credible-source',
    credibleForTypeValidation: true,
  });
  requireCondition(
    resultIsReleaseBlockingFailure(first, scenario, fixtureSources.get(first.referenceSource)),
    'release-blocking fixture must fail when credible mandatory tolerance is exceeded even if result flag drifts',
  );
}

function main() {
  const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
  const root = path.join(repo, 'docs', 'validation', 'aircraft', 'flying_trainer_one');
  const registry = loadJson(path.join(root, 'reference-registry.json'));
  const scenarios = loadJson(path.join(root, 'validation-scenarios.json'));
  const results = loadJson(path.join(root, 'validation-results.json'));
  const report = readFileSync(path.join(root, 'aircraft-validation-report.md'), 'utf8');

  const sources = validateReferenceRegistry(registry);
  const scenarioDefinitions = validateScenarios(scenarios, sources);
  validateResults(root, results, scenarioDefinitions, sources);
  validateReleaseBlockFixture(results, sources);
  requireCondition(
    report.includes('not labeled faithful'),
    'aircraft validation report must state that no faithful type claim is made',
  );
  requireCondition(
    report.includes('passed_with_approved_exclusions'),
    'aircraft validation report must state the release-blocking result',
  );
}

try {
  main();
} catch (error) {
  console.error(error.message);
  process.exit(1);
}
