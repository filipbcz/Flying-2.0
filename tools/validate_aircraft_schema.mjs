import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const schemaCache = new Map();

function fail(message) {
  throw new Error(message);
}

function readJson(relativePath) {
  return JSON.parse(fs.readFileSync(path.join(repoRoot, relativePath), "utf8"));
}

function loadSchema(absolutePath) {
  const normalized = path.normalize(absolutePath);
  if (!schemaCache.has(normalized)) {
    schemaCache.set(normalized, JSON.parse(fs.readFileSync(normalized, "utf8")));
  }
  return schemaCache.get(normalized);
}

function resolvePointer(root, pointer) {
  return pointer
    .slice(2)
    .split("/")
    .reduce((node, segment) => {
      const key = segment.replaceAll("~1", "/").replaceAll("~0", "~");
      if (node === undefined || node === null || !(key in node)) {
        fail(`schema reference ${pointer} cannot be resolved`);
      }
      return node[key];
    }, root);
}

function resolveRef(ref, root, basePath) {
  if (ref.startsWith("#/")) {
    return { schema: resolvePointer(root, ref), root, basePath };
  }
  const [fileRef, pointer = ""] = ref.split("#");
  const externalPath = path.resolve(path.dirname(basePath), fileRef);
  const externalRoot = loadSchema(externalPath);
  return {
    schema: pointer ? resolvePointer(externalRoot, `#${pointer}`) : externalRoot,
    root: externalRoot,
    basePath: externalPath,
  };
}

function typeMatches(value, expected) {
  if (expected === "array") {
    return Array.isArray(value);
  }
  if (expected === "object") {
    return value !== null && typeof value === "object" && !Array.isArray(value);
  }
  if (expected === "integer") {
    return Number.isInteger(value);
  }
  if (expected === "number") {
    return typeof value === "number" && Number.isFinite(value);
  }
  if (expected === "string") {
    return typeof value === "string";
  }
  if (expected === "boolean") {
    return typeof value === "boolean";
  }
  return true;
}

function validate(value, schema, root, basePath, at = "$") {
  if (!schema || typeof schema !== "object") {
    return [];
  }
  if (schema.$ref) {
    const resolved = resolveRef(schema.$ref, root, basePath);
    return validate(value, resolved.schema, resolved.root, resolved.basePath, at);
  }

  const errors = [];
  const expectedType = schema.type;
  if (expectedType !== undefined) {
    const allowed = Array.isArray(expectedType) ? expectedType : [expectedType];
    if (!allowed.some((type) => typeMatches(value, type))) {
      return [`${at} must be ${allowed.join(" or ")}`];
    }
  }
  if ("const" in schema && value !== schema.const) {
    errors.push(`${at} must equal ${JSON.stringify(schema.const)}`);
  }
  if (Array.isArray(schema.enum) && !schema.enum.includes(value)) {
    errors.push(`${at} must be one of ${JSON.stringify(schema.enum)}`);
  }

  if (Array.isArray(schema.allOf)) {
    for (const subschema of schema.allOf) {
      if (subschema.if && subschema.then) {
        if (validate(value, subschema.if, root, basePath, at).length === 0) {
          errors.push(...validate(value, subschema.then, root, basePath, at));
        }
      } else {
        errors.push(...validate(value, subschema, root, basePath, at));
      }
    }
  }
  if (schema.not && validate(value, schema.not, root, basePath, at).length === 0) {
    errors.push(`${at} must not match prohibited schema`);
  }

  if (Array.isArray(value)) {
    if (schema.minItems !== undefined && value.length < schema.minItems) {
      errors.push(`${at} must contain at least ${schema.minItems} items`);
    }
    if (schema.uniqueItems) {
      const seen = new Set();
      for (const item of value) {
        const encoded = JSON.stringify(item);
        if (seen.has(encoded)) {
          errors.push(`${at} must contain unique items`);
          break;
        }
        seen.add(encoded);
      }
    }
    if (schema.items) {
      value.forEach((item, index) => {
        errors.push(...validate(item, schema.items, root, basePath, `${at}[${index}]`));
      });
    }
    if (schema.contains) {
      const matched = value.some(
        (item, index) => validate(item, schema.contains, root, basePath, `${at}[${index}]`).length === 0,
      );
      if (!matched) {
        errors.push(`${at} must contain an item matching the required schema`);
      }
    }
  }

  if (value !== null && typeof value === "object" && !Array.isArray(value)) {
    for (const field of schema.required ?? []) {
      if (!(field in value)) {
        errors.push(`${at}.${field} is required`);
      }
    }
    const properties = schema.properties ?? {};
    if (schema.additionalProperties === false) {
      for (const field of Object.keys(value)) {
        if (!(field in properties)) {
          errors.push(`${at}.${field} is not allowed`);
        }
      }
    }
    for (const [field, propertySchema] of Object.entries(properties)) {
      if (field in value) {
        errors.push(...validate(value[field], propertySchema, root, basePath, `${at}.${field}`));
      }
    }
  }

  if (typeof value === "string" && schema.minLength !== undefined && value.length < schema.minLength) {
    errors.push(`${at} must not be empty`);
  }
  if (typeof value === "string" && schema.pattern !== undefined) {
    const pattern = new RegExp(schema.pattern);
    if (!pattern.test(value)) {
      errors.push(`${at} must match ${schema.pattern}`);
    }
  }
  if (typeof value === "number" && schema.minimum !== undefined && value < schema.minimum) {
    errors.push(`${at} must be >= ${schema.minimum}`);
  }

  return errors;
}

function assertValid(schema, basePath, document, label) {
  const errors = validate(document, schema, schema, basePath);
  if (errors.length > 0) {
    fail(`${label} should be valid: ${errors.slice(0, 8).join("; ")}`);
  }
}

function assertInvalid(schema, basePath, document, label) {
  const errors = validate(document, schema, schema, basePath);
  if (errors.length === 0) {
    fail(`${label} should be rejected by aircraft schema`);
  }
}

function assertProductionValidationReportsExist(document) {
  const validationSources = document.sourceReferences.filter((source) => source.usedFor.includes("validation"));
  if (validationSources.length === 0) {
    fail("production aircraft config must link validation source references to repository reports");
  }
  for (const source of validationSources) {
    const resolved = path.resolve(repoRoot, source.document);
    if (!resolved.startsWith(`${repoRoot}${path.sep}`) || !fs.existsSync(resolved)) {
      fail(`validation source ${source.id} must reference an existing repository-local artifact`);
    }
  }
}

function approvedSource(overrides = {}) {
  return {
    id: "flying-trainer-one-validation-suite-v1",
    title: "Flying Trainer One Validation Suite Approval",
    document: "docs/validation/aircraft/flying_trainer_one/aircraft-validation-report.md",
    license: "Flying repository",
    permittedUse: "Authoritative validation evidence for Flying Trainer One fidelity claims.",
    provenance: "Generated by the aircraft validation suite and approved for fidelity gating.",
    confidence: "high",
    usedFor: ["validation"],
    approvedForFaithfulClaim: true,
    ...overrides,
  };
}

const rootSchemaPath = path.join(repoRoot, "schemas", "aircraft.schema.json");
const schema = loadSchema(rootSchemaPath);
const production = readJson("core_sim/aircraft/flying_trainer_one/aircraft-config.json");

assertValid(schema, rootSchemaPath, production, "production unvalidated aircraft config");
assertProductionValidationReportsExist(production);

const faithful = structuredClone(production);
faithful.aircraft.validation.status = "faithful";
faithful.aircraft.validation.suiteStatus = "passed";
faithful.aircraft.validation.approvedReferences = ["flying-trainer-one-validation-suite-v1"];
faithful.sourceReferences = [...faithful.sourceReferences, approvedSource()];
assertValid(schema, rootSchemaPath, faithful, "faithful aircraft config with approved validation reference");

const unknownReference = structuredClone(faithful);
unknownReference.aircraft.validation.approvedReferences = ["unknown-validation-reference"];
assertInvalid(schema, rootSchemaPath, unknownReference, "faithful config with unknown approvedReferences id");

const unapprovedReference = structuredClone(faithful);
unapprovedReference.sourceReferences = [...production.sourceReferences, approvedSource({ approvedForFaithfulClaim: false })];
assertInvalid(schema, rootSchemaPath, unapprovedReference, "faithful config with unapproved source reference");

const missingValidationUse = structuredClone(faithful);
missingValidationUse.sourceReferences = [...production.sourceReferences, approvedSource({ usedFor: ["aerodynamics"] })];
assertInvalid(schema, rootSchemaPath, missingValidationUse, "faithful config with source lacking validation use");

const missingValidationReportLink = structuredClone(faithful);
missingValidationReportLink.sourceReferences = [
  ...production.sourceReferences,
  approvedSource({ document: "internal:flying/validation-suite" }),
];
assertInvalid(
  schema,
  rootSchemaPath,
  missingValidationReportLink,
  "faithful config with missing repository-local validation report link",
);

const externalOnlyValidationReportLink = structuredClone(faithful);
externalOnlyValidationReportLink.sourceReferences = [
  ...production.sourceReferences,
  approvedSource({ document: "https://example.invalid/aircraft-validation-report.md" }),
];
assertInvalid(
  schema,
  rootSchemaPath,
  externalOnlyValidationReportLink,
  "faithful config with external-only validation report link",
);
