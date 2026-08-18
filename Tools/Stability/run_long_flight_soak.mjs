#!/usr/bin/env node

import { spawn } from "node:child_process";
import fs from "node:fs";
import path from "node:path";

function fail(message, code = 1) {
  console.error(`run_long_flight_soak: ${message}`);
  process.exit(code);
}

function parseArgs(argv) {
  const args = {
    durationHours: 10,
    pollSeconds: 60,
    simArgs: [],
  };
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if (arg === "--sim-command") {
      args.simCommand = argv[++index];
    } else if (arg === "--report") {
      args.report = argv[++index];
    } else if (arg === "--scenario-id") {
      args.scenarioId = argv[++index];
    } else if (arg === "--core-sim-version") {
      args.coreSimVersion = argv[++index];
    } else if (arg === "--data-version") {
      args.dataVersion = argv[++index];
    } else if (arg === "--config-version") {
      args.configVersion = argv[++index];
    } else if (arg === "--input-profile") {
      args.inputProfile = argv[++index];
    } else if (arg === "--weather-seed") {
      args.weatherSeed = argv[++index];
    } else if (arg === "--duration-hours") {
      args.durationHours = Number(argv[++index]);
    } else if (arg === "--poll-seconds") {
      args.pollSeconds = Number(argv[++index]);
    } else if (arg === "--") {
      args.simArgs = argv.slice(index + 1);
      break;
    } else {
      fail(`unsupported argument: ${arg}`, 2);
    }
  }

  for (const required of [
    "simCommand",
    "report",
    "scenarioId",
    "coreSimVersion",
    "dataVersion",
    "configVersion",
    "inputProfile",
    "weatherSeed",
  ]) {
    if (!args[required]) {
      fail(`missing required --${required.replace(/[A-Z]/g, (letter) => `-${letter.toLowerCase()}`)}`, 2);
    }
  }
  if (!Number.isFinite(args.durationHours) || args.durationHours <= 0) {
    fail("--duration-hours must be positive", 2);
  }
  if (!Number.isFinite(args.pollSeconds) || args.pollSeconds <= 0) {
    fail("--poll-seconds must be positive", 2);
  }
  return args;
}

function readProcessMemory(pid) {
  if (process.platform !== "linux") {
    return null;
  }
  try {
    const statm = fs.readFileSync(`/proc/${pid}/statm`, "utf8").trim().split(/\s+/);
    const residentPages = Number(statm[1]);
    if (!Number.isFinite(residentPages)) {
      return null;
    }
    return {
      source: "procfs.statm",
      residentMiB: Math.round((residentPages * 4096) / 1024 / 1024),
    };
  } catch {
    return null;
  }
}

function writeReport(reportPath, report) {
  fs.mkdirSync(path.dirname(path.resolve(reportPath)), { recursive: true });
  fs.writeFileSync(reportPath, `${JSON.stringify(report, null, 2)}\n`);
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  const startedAt = new Date();
  const report = {
    schemaVersion: "flying.long-flight-soak-report.v1",
    scenarioId: args.scenarioId,
    startedAtUtc: startedAt.toISOString().replace(/\.\d{3}Z$/, "Z"),
    completedAtUtc: null,
    durationHoursRequired: args.durationHours,
    durationHoursActual: 0,
    identity: {
      coreSimVersion: args.coreSimVersion,
      dataVersion: args.dataVersion,
      configVersion: args.configVersion,
      inputProfile: args.inputProfile,
      weatherSeed: args.weatherSeed,
    },
    process: {
      command: args.simCommand,
      args: args.simArgs,
      exitCode: null,
      signal: null,
      crashed: false,
    },
    memorySamples: [],
    hitches: [],
    streamingErrors: [],
    replayHashes: [],
  };

  const child = spawn(args.simCommand, args.simArgs, {
    stdio: ["ignore", "pipe", "pipe"],
    shell: process.platform === "win32",
    env: {
      ...process.env,
      FLYING_SOAK_DURATION_HOURS: String(args.durationHours),
      FLYING_SOAK_SCENARIO_ID: args.scenarioId,
      FLYING_WEATHER_SEED: String(args.weatherSeed),
    },
  });

  const parseLine = (line) => {
    const trimmed = line.trim();
    if (!trimmed) {
      return;
    }
    const jsonStart = trimmed.indexOf("{");
    if (jsonStart < 0) {
      return;
    }
    try {
      const event = JSON.parse(trimmed.slice(jsonStart));
      if (event.type === "memory") {
        report.memorySamples.push(event);
      } else if (event.type === "hitch") {
        report.hitches.push(event);
      } else if (event.type === "streaming-error") {
        report.streamingErrors.push(event);
      } else if (event.type === "replay-hash") {
        report.replayHashes.push(event);
      }
    } catch {
      // Non-JSON simulator output is intentionally ignored.
    }
  };

  for (const stream of [child.stdout, child.stderr]) {
    let buffer = "";
    stream.setEncoding("utf8");
    stream.on("data", (chunk) => {
      buffer += chunk;
      const lines = buffer.split(/\r?\n/);
      buffer = lines.pop() ?? "";
      for (const line of lines) {
        parseLine(line);
      }
    });
    stream.on("end", () => parseLine(buffer));
  }

  const poll = setInterval(() => {
    const memory = readProcessMemory(child.pid);
    if (memory) {
      report.memorySamples.push({
        type: "memory",
        capturedAtUtc: new Date().toISOString().replace(/\.\d{3}Z$/, "Z"),
        ...memory,
      });
    }
  }, args.pollSeconds * 1000);

  const exit = await new Promise((resolve) => {
    child.on("exit", (exitCode, signal) => resolve({ exitCode, signal }));
    child.on("error", (error) => resolve({ exitCode: 127, signal: null, error }));
  });
  clearInterval(poll);

  const completedAt = new Date();
  report.completedAtUtc = completedAt.toISOString().replace(/\.\d{3}Z$/, "Z");
  report.durationHoursActual = (completedAt.getTime() - startedAt.getTime()) / 3600000;
  report.process.exitCode = exit.exitCode;
  report.process.signal = exit.signal;
  report.process.crashed = exit.exitCode !== 0 || exit.signal !== null || Boolean(exit.error);
  if (exit.error) {
    report.process.error = exit.error.message;
  }

  writeReport(args.report, report);
  if (report.process.crashed) {
    fail(`simulator exited abnormally; report written to ${args.report}`);
  }
}

main().catch((error) => fail(error.message));
