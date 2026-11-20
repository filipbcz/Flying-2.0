# Scenario package integration checkpoint

Run the current-session compatibility/startup preflight and focused native gate
as described in `docs/scenario-installed-packages.md`, then run the widget proof
through the structured Unreal Python adapter. Do not reuse an earlier session's
widget report as proof of a new build.

On long leased checkout paths, pass `-DDC=NoShared` with `-NullRHI` to
the structured Unreal Python adapter. The default installed filesystem cache
can exceed Unreal's 119-character cache-path limit even with
`-DDC-ForceMemoryCache`. NoShared excludes that filesystem cache without
changing the canonical startup assets or the project configuration.

Pass an absolute checkout path to `-ExecutePythonScript`: Unreal resolves relative
script paths against its engine executable directory, not this repository. Require
the newly generated `widget-integration.json` as proof; the Editor can exit zero
even when it could not load the script.

Write the focused CTest JUnit report to
`.forgemind-tmp/scenario-package-integration/native-regressions.xml`, and preserve
the unmodified managed production input under `installed-package` in that
same evidence directory. Then run from the repository root:

```
node Tools/ScenarioPackageIntegration/record_checkpoint.mjs
node Tools/ScenarioPackageIntegration/validate_evidence.mjs
```

The recorder binds the widget report to the current compiled Editor module and
records source identities using explicit `utf8-lf` hashes. Git checkout CRLF
conversion therefore does not invalidate equivalent source. Installed packages,
startup assets and proof artifacts retain byte-exact SHA-256 identities. The
validator rejects source changes, altered artifacts and failed/skipped native
regressions. Publish the completed directory to persistent project data at
`artifacts/scenario-package-integration`. This checkpoint covers package/widget
integration only, not world rendering, performance or installed Shipping acceptance.
