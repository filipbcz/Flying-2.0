#!/usr/bin/env node
import { execFileSync } from "node:child_process";
import { createRequire } from "node:module";
import fs from "node:fs";
import path from "node:path";

function fail(message) {
  console.error(message);
  process.exit(1);
}

function read(file) {
  return fs.readFileSync(file, "utf8");
}

function loadYamlParser() {
  const require = createRequire(import.meta.url);
  for (const specifier of ["yaml", "/app/node_modules/yaml"]) {
    try {
      return require(specifier);
    } catch (error) {
      if (error.code !== "MODULE_NOT_FOUND") {
        throw error;
      }
    }
  }
  fail("YAML parser dependency 'yaml' is not available");
}

function parseWorkflowYaml(file) {
  const parser = loadYamlParser();
  try {
    return parser.parse(read(file));
  } catch (error) {
    fail(`${file} is not valid YAML: ${error.message}`);
  }
}

function requireMatch(text, pattern, message) {
  if (!pattern.test(text)) {
    fail(message);
  }
}

function requireEqual(actual, expected, message) {
  if (actual !== expected) {
    fail(`${message}: expected ${expected}, got ${actual}`);
  }
}

function hasOwn(object, key) {
  return Object.prototype.hasOwnProperty.call(object ?? {}, key);
}

function requireEnv(job, name) {
  if (job?.env?.FLYING_RUNTIME_NETWORK !== "OFF") {
    fail(`${name} job must keep runtime network off`);
  }
  if (job?.env?.FLYING_REQUIRE_MAP_API_KEYS !== "OFF") {
    fail(`${name} job must not require map API keys`);
  }
}

function requireStepRun(job, command, message) {
  if (!Array.isArray(job?.steps) || !job.steps.some((step) => step.run === command)) {
    fail(message);
  }
}

const nativeWorkflow = parseWorkflowYaml(".github/workflows/native-skeleton.yml");
const soakWorkflow = parseWorkflowYaml(".github/workflows/native-soak.yml");

if (!hasOwn(nativeWorkflow.on, "pull_request") || !hasOwn(nativeWorkflow.on, "workflow_dispatch")) {
  fail("native workflow must run for pull_request and workflow_dispatch");
}
if (!hasOwn(nativeWorkflow.on, "push")) {
  fail("native workflow must keep push trigger for main");
}
if (JSON.stringify(nativeWorkflow.on.push?.branches) !== JSON.stringify(["main"])) {
  fail("native workflow push trigger must be limited to main");
}

const smokeJob = nativeWorkflow.jobs?.smoke;
requireEqual(smokeJob?.["runs-on"], "windows-latest", "native smoke job must use the Windows runner");
requireEqual(smokeJob?.["timeout-minutes"], 10, "native smoke job must have a 10 minute timeout");
requireEnv(smokeJob, "native smoke");
requireStepRun(smokeJob, "ctest --preset smoke --output-on-failure", "native smoke workflow must run the smoke preset");

if (!hasOwn(soakWorkflow.on, "workflow_dispatch")) {
  fail("soak workflow must support manual runs");
}
if (!Array.isArray(soakWorkflow.on?.schedule) || soakWorkflow.on.schedule.length === 0) {
  fail("soak workflow must support scheduled runs");
}
if (!soakWorkflow.on.schedule.every((entry) => typeof entry.cron === "string" && entry.cron.length > 0)) {
  fail("soak workflow schedule must use cron entries");
}
const soakJob = soakWorkflow.jobs?.soak;
requireEqual(soakJob?.["runs-on"], "windows-latest", "soak job must use the Windows runner");
if (!Number.isInteger(soakJob?.["timeout-minutes"])) {
  fail("soak job must have a finite timeout");
}
if (soakJob["timeout-minutes"] < 600 || soakJob["timeout-minutes"] > 1440) {
  fail("soak timeout must be long enough for the ten-hour test but finite");
}
requireEnv(soakJob, "soak");
requireStepRun(soakJob, "ctest --preset soak --output-on-failure", "soak workflow must run the soak preset");

const presets = JSON.parse(read("CMakePresets.json"));
const smokePreset = presets.testPresets?.find((preset) => preset.name === "smoke");
const soakPreset = presets.testPresets?.find((preset) => preset.name === "soak");
if (!smokePreset || smokePreset.filter?.include?.label !== "smoke") {
  fail("smoke preset must explicitly include the smoke label");
}
if (!soakPreset || soakPreset.filter?.include?.label !== "soak") {
  fail("soak preset must explicitly include the soak label");
}

execFileSync("cmake", ["--preset", "test"], { stdio: "inherit" });

const smokeList = execFileSync("ctest", ["--preset", "smoke", "-N"], { encoding: "utf8" });
const soakList = execFileSync("ctest", ["--preset", "soak", "-N"], { encoding: "utf8" });
if (smokeList.includes("flying.core_sim.performance_timing")) {
  fail("smoke preset must not select the ten-hour performance timing soak test");
}
if (!soakList.includes("flying.core_sim.performance_timing")) {
  fail("soak preset must select the ten-hour performance timing test");
}

const ctestFile = read(path.join("out", "build", "test", "tests", "CTestTestfile.cmake"));
const propertyMatches = [...ctestFile.matchAll(/set_tests_properties\(\[=\[([^\]]+)]=] PROPERTIES\s+([^\n]+)\n/g)];
const properties = new Map(propertyMatches.map((match) => [match[1], match[2]]));
for (const [name, props] of properties) {
  if (/\bLABELS "([^"]*\bsmoke\b[^"]*)"/.test(props)) {
    const timeout = props.match(/\bTIMEOUT "([0-9]+)"/);
    if (!timeout) {
      fail(`smoke test ${name} is missing a CTest TIMEOUT`);
    }
    if (Number(timeout[1]) < 1 || Number(timeout[1]) > 60) {
      fail(`smoke test ${name} has unreasonable CTest TIMEOUT ${timeout[1]}`);
    }
  }
}

const performanceProps = properties.get("flying.core_sim.performance_timing");
if (!performanceProps) {
  fail("flying.core_sim.performance_timing test is missing");
}
requireMatch(performanceProps, /\bLABELS "soak"/, "performance timing test must be labeled soak");
requireMatch(performanceProps, /\bTIMEOUT "43200"/, "performance timing test must keep its finite 12 hour CTest timeout");
