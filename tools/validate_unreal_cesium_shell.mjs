#!/usr/bin/env node

import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const requireUnrealBuildRun = process.argv.includes("--require-unreal-build-run");
const runArtifactPath = path.join(
  repoRoot,
  "docs",
  "validation",
  "release",
  "unreal-cesium-shell-win64-development-run.json",
);
const startupMarker = "Flying georeferenced simulator shell initialized";

function fail(message) {
  console.error(`validate_unreal_cesium_shell: ${message}`);
  process.exit(1);
}

function read(relativePath) {
  try {
    return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
  } catch {
    fail(`missing required file: ${relativePath}`);
  }
}

function requireCondition(condition, message) {
  if (!condition) {
    fail(message);
  }
}

function firstExisting(paths) {
  return paths.find((candidate) => candidate && fs.existsSync(candidate));
}

function toRepoPath(absolutePath) {
  return path.relative(repoRoot, absolutePath).replaceAll(path.sep, "/");
}

function findUnrealRoot() {
  const candidates = [
    process.env.UE_5_8_ROOT,
    process.env.UNREAL_ENGINE_5_8_ROOT,
    process.env.UNREAL_ENGINE_ROOT,
    process.env.UE_ROOT,
  ];

  if (process.platform === "win32") {
    candidates.push(
      "C:\\Program Files\\Epic Games\\UE_5.8",
      "C:\\Program Files\\Epic Games\\UE_5.8EA",
    );
  }

  return firstExisting(candidates);
}

function runCommand(command, args, label) {
  const result = spawnSync(command, args, {
    cwd: repoRoot,
    encoding: "utf8",
    shell: process.platform === "win32",
    stdio: ["ignore", "pipe", "pipe"],
  });

  if (result.status !== 0) {
    const output = `${result.stdout ?? ""}\n${result.stderr ?? ""}`.trim();
    fail(`${label} failed with exit code ${result.status}: ${output}`);
  }

  return `${result.stdout ?? ""}\n${result.stderr ?? ""}`;
}

function assertUnrealVersion(unrealRoot) {
  const versionPath = path.join(unrealRoot, "Engine", "Build", "Build.version");
  const version = JSON.parse(fs.readFileSync(versionPath, "utf8"));
  requireCondition(
    version.MajorVersion === 5 && version.MinorVersion === 8,
    `Unreal toolchain must be UE 5.8, found ${version.MajorVersion}.${version.MinorVersion}`,
  );
  return version;
}

function assertWindows11Host() {
  const releaseParts = os.release().split(".").map((part) => Number.parseInt(part, 10));
  const buildNumber = releaseParts[2] ?? 0;
  requireCondition(buildNumber >= 22000, `Win64 Unreal build/run validation requires Windows 11, found ${os.release()}`);
  return {
    os: "Windows 11",
    processPlatform: process.platform,
    osRelease: os.release(),
    osVersion: os.version(),
    hostname: os.hostname(),
  };
}

function findBuiltExecutable() {
  const binariesDir = path.join(repoRoot, "Binaries", "Win64");
  const candidates = [
    path.join(binariesDir, "Flying-Win64-Development.exe"),
    path.join(binariesDir, "Flying.exe"),
  ];
  return firstExisting(candidates);
}

function extractLogExcerpt(logText) {
  const markerIndex = logText.indexOf(startupMarker);
  requireCondition(markerIndex >= 0, "Win64 Development run log did not prove georeferenced startup shell initialization");

  const start = Math.max(0, markerIndex - 240);
  const end = Math.min(logText.length, markerIndex + startupMarker.length + 240);
  return logText.slice(start, end);
}

function writeRunArtifact({ host, unrealVersion, executable, runLog, logExcerpt }) {
  fs.mkdirSync(path.dirname(runArtifactPath), { recursive: true });
  fs.writeFileSync(
    runArtifactPath,
    `${JSON.stringify(
      {
        schemaVersion: "flying.unreal-cesium-shell-run.v1",
        command: "node tools/validate_unreal_cesium_shell.mjs --require-unreal-build-run",
        exitCode: 0,
        executedAtUtc: new Date().toISOString().replace(/\.\d{3}Z$/, "Z"),
        host,
        unrealEngine: {
          majorVersion: unrealVersion.MajorVersion,
          minorVersion: unrealVersion.MinorVersion,
          patchVersion: unrealVersion.PatchVersion,
          changelist: unrealVersion.Changelist,
          branchName: unrealVersion.BranchName,
        },
        build: {
          targetPlatform: "Win64",
          configuration: "Development",
          projectPath: "Flying.uproject",
          executablePath: toRepoPath(executable),
        },
        launch: {
          logPath: toRepoPath(runLog),
          logExcerpt,
        },
      },
      null,
      2,
    )}\n`,
    "utf8",
  );
}

function validateWin64DevelopmentBuildRun() {
  if (process.platform !== "win32") {
    if (requireUnrealBuildRun) {
      fail("Win64 Unreal build/run validation requires Windows");
    }
    console.log("validate_unreal_cesium_shell: Win64 Unreal build/run skipped on non-Windows host");
    return;
  }

  const host = assertWindows11Host();
  const unrealRoot = findUnrealRoot();
  if (!unrealRoot) {
    if (requireUnrealBuildRun) {
      fail("UE 5.8 toolchain not found; set UE_5_8_ROOT or UNREAL_ENGINE_ROOT");
    }
    console.log("validate_unreal_cesium_shell: Win64 Unreal build/run skipped because UE 5.8 was not found");
    return;
  }

  const unrealVersion = assertUnrealVersion(unrealRoot);

  const buildScript = path.join(unrealRoot, "Engine", "Build", "BatchFiles", "Build.bat");
  requireCondition(fs.existsSync(buildScript), `missing Unreal build script: ${buildScript}`);

  const projectPath = path.join(repoRoot, "Flying.uproject");
  runCommand(
    buildScript,
    ["Flying", "Win64", "Development", projectPath, "-WaitMutex", "-NoHotReload"],
    "Unreal Win64 Development build",
  );

  const executable = findBuiltExecutable();
  requireCondition(executable, "Win64 Development executable was not produced under Binaries/Win64");

  const savedLogDir = path.join(repoRoot, "Saved", "Logs");
  fs.mkdirSync(savedLogDir, { recursive: true });
  const runLog = path.join(savedLogDir, "FlyingCesiumShellValidation.log");
  if (fs.existsSync(runLog)) {
    fs.rmSync(runLog);
  }

  runCommand(
    executable,
    [
      "-game",
      "-nullrhi",
      "-nosound",
      "-unattended",
      "-stdout",
      `-abslog=${runLog}`,
      "-ExecCmds=quit",
    ],
    "Win64 Development headless launch",
  );

  const logText = read(path.relative(repoRoot, runLog));
  const logExcerpt = extractLogExcerpt(logText);
  writeRunArtifact({ host, unrealVersion, executable, runLog, logExcerpt });
}

function walk(dir) {
  const absolute = path.join(repoRoot, dir);
  if (!fs.existsSync(absolute)) {
    fail(`missing required directory: ${dir}`);
  }

  const files = [];
  for (const entry of fs.readdirSync(absolute, { withFileTypes: true })) {
    const relative = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      files.push(...walk(relative));
    } else {
      files.push(relative);
    }
  }
  return files;
}

const project = JSON.parse(read("Flying.uproject"));
requireCondition(project.EngineAssociation === "5.8", "Flying.uproject must target Unreal Engine 5.8");
requireCondition(project.TargetPlatforms?.includes("Win64"), "Flying.uproject must target Win64");
requireCondition(
  project.Modules?.some((module) => module.Name === "FlyingPresentation" && module.Type === "Runtime"),
  "Flying.uproject must declare the FlyingPresentation runtime module",
);
requireCondition(
  project.Plugins?.some((plugin) => plugin.Name === "CesiumForUnreal" && plugin.Enabled === true),
  "Flying.uproject must enable CesiumForUnreal",
);

const engine = read("Config/DefaultEngine.ini");
requireCondition(
  engine.includes("GameDefaultMap=/Engine/Maps/Templates/OpenWorld"),
  "DefaultEngine.ini must open a startup scene",
);
requireCondition(
  engine.includes("GlobalDefaultGameMode=/Script/FlyingPresentation.FlyingStartupGameMode"),
  "DefaultEngine.ini must bind the presentation startup game mode",
);
requireCondition(
  engine.includes("CesiumForUnrealMinimumVersion=2.28.0"),
  "DefaultEngine.ini must record Cesium for Unreal 2.28+ compatibility",
);
requireCondition(
  !/https?:\/\/|api[_-]?key|access[_-]?token|mapbox|ionasset/i.test(engine),
  "DefaultEngine.ini must not configure remote map APIs, tokens, or ion assets",
);

const build = read("Source/FlyingPresentation/FlyingPresentation.Build.cs");
for (const token of [
  "CppStandardVersion.Cpp20",
  "\"CesiumRuntime\"",
  "FLYING_PRESENTATION_OFFLINE_ONLY=1",
]) {
  requireCondition(build.includes(token), `Build.cs missing required token: ${token}`);
}

const georefHeader = read("Source/FlyingPresentation/Public/FlyingCesiumGeoreferenceComponent.h");
for (const token of [
  "TransformEcefPositionToUnreal",
  "TransformEcefDirectionToUnreal",
  "ACesiumGeoreference",
]) {
  requireCondition(georefHeader.includes(token), `georeference public API missing token: ${token}`);
}

const georefCpp = read("Source/FlyingPresentation/Private/FlyingCesiumGeoreferenceComponent.cpp");
for (const token of [
  "SetOriginLongitudeLatitudeHeight",
  "TransformEarthCenteredEarthFixedPositionToUnreal",
  "TransformEarthCenteredEarthFixedDirectionToUnreal",
]) {
  requireCondition(georefCpp.includes(token), `georeference implementation missing token: ${token}`);
}

const gameMode = read("Source/FlyingPresentation/Private/FlyingStartupGameMode.cpp");
requireCondition(gameMode.includes("SpawnActor<ACesiumGeoreference>"), "startup shell must create a Cesium georeference");
requireCondition(gameMode.includes("ConfigureStartupOrigin"), "startup shell must configure georeference origin");
requireCondition(
  gameMode.includes(startupMarker),
  "startup shell must emit runtime georeference evidence",
);

for (const sourceFile of walk("Source/FlyingPresentation")) {
  if (!/\.(h|hpp|cpp|cs)$/.test(sourceFile)) {
    continue;
  }
  const text = read(sourceFile);
  requireCondition(
    !/#\s*include\s+[<"][^>"]*(?:core_sim\/src|CoreSimStandalone|Source\/CoreSim\/Private)[^>"]*[>"]/.test(text),
    `Presentation module includes CoreSim private implementation header: ${sourceFile}`,
  );
}

validateWin64DevelopmentBuildRun();

console.log("validate_unreal_cesium_shell: ok");
