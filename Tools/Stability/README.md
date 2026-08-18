# Long Flight Stability QA

This gate records and validates release-candidate long-flight stability evidence.

Run the soak wrapper against the signed Win64 Shipping executable in the clean Windows 11 release environment:

```powershell
node Tools/Stability/run_long_flight_soak.mjs `
  --sim-command .\Flying-Win64-Shipping.exe `
  --duration-hours 10 `
  --scenario-id regional-cross-country-soak `
  --core-sim-version 1.0.0-rc `
  --data-version pilot-region-ceska-trebova-2026-08-18 `
  --config-version shipping-high `
  --input-profile deterministic-soak-v1 `
  --weather-seed 424242 `
  --report Reports/Stability/long-flight-soak.json
```

Validate the resulting soak evidence:

```sh
node Tools/Stability/validate_soak_report.mjs --report Reports/Stability/long-flight-soak.json
```

Validate the supported GPU and device QA matrix:

```sh
node Tools/Stability/validate_qa_matrix.mjs --matrix Reports/Stability/qa-matrix.json
```
