# Private pilot correction validation

Use the directly installed Python 3.13 interpreter, not the broken py launcher.
On this lease set PYTHONUSERBASE to C:/Users/borek-fil/AppData/Roaming/Python.

Authoritative acceptance:

```powershell
$env:PYTHONUSERBASE='C:/Users/borek-fil/AppData/Roaming/Python'
& 'C:/Users/borek-fil/AppData/Local/Programs/Python/Python313/python.exe' Tools/data_pipeline/build_airport_navigation.py private-package-gate --output .forgemind-tmp/managed/private-acceptance
exit $LASTEXITCODE
```

Executed successfully on November 19, 2026: 31 real-source records; 172 package
files; one unverified SLZ/runway/start; nine local layers; 1,027 PNG tiles; zero
missing records against the bounded master list; fresh archive relocation passed.
Inputs are materialized from the three published project-data roots documented
in README.md. The command is repeatable and creates a fresh relocation directory.

The focused test suite also checks source/installed checksum tampering, synthetic
and unidentified provenance rejection, stale passing report replacement,
derived-value tampering, unsafe archive paths, local MBTiles/style, and strict
production/private gate separation.

Rasterio warns that PNGs lack intrinsic georeferencing. The acquired Ortofoto uses
its checksum-bound export extent, and map tiles use MBTiles TMS coordinates.

The unchanged production-gate command remains blocked by absent survey control.
That diagnostic is preserved in production-gate-report.json. This task accepts
private derived geometry with honest uncertainty; it does not accept regulator
verification, survey-grade geometry, operational charts or release-candidate flight.
