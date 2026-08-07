# Flying Release License Inventory

Document ID: REL-29-LICENSE-INVENTORY  
Status: Release evidence candidate  
Release family: Flying 1.0.0 Win64 offline release  
Prepared: 2026-08-07  
Legal note: This inventory records engineering license facts and required notices. It is not legal advice.

## Summary

Flying release artifacts must carry license evidence for code libraries, build and packaging tools, and redistributed data packages. The authoritative governance gates remain `docs/governance/m0-legal-data-gate.md`, `docs/governance/m0-aircraft-data-gate.md`, `third_party/dependencies.yml`, and `third_party/NOTICES.md`.

## Code Libraries And Runtime Components

| Component | Role | Version or bound | Distribution status | Recorded license fact | Required release action |
| --- | --- | --- | --- | --- | --- |
| Flying native code | CoreSim, geo terrain, GIS pipeline and Unreal presentation module authored in this repository. | Repository version `0.1.0`; release packaging records external build ID. | Redistributed as project binaries and source where applicable. | Project license is not declared in this repository. | Release owner must attach the project license or keep distribution internal until the project license is declared. |
| Unreal Engine | Win64 rendering, audio, UI, input, packaging and crash diagnostics runtime. | 5.8.x per `unreal/Flying.uproject` and `third_party/dependencies.yml`. | Developer-installed through Epic-approved channels; not vendored by this repository. | Unreal Engine EULA applies. | Preserve Epic/Unreal notices required by the release channel and do not redistribute engine materials outside allowed terms. |
| Cesium for Unreal | Georeferencing and ECEF/WGS-84 integration. | `>=2.28.0 <3.0.0` intent; plugin enabled in `unreal/Flying.uproject`. | Installed as an Unreal plugin, not vendored here. | Preserve Cesium for Unreal notices and licenses. | Include Cesium for Unreal notice text from the installed plugin in the shipped notices bundle. |
| ProceduralMeshComponent | Runtime terrain mesh presentation. | Unreal Engine plugin for UE 5.8. | Engine plugin. | Covered by applicable Unreal Engine terms. | Include with Unreal notice surface as required by Epic terms. |
| UMG, SQLiteCore, Json, JsonUtilities, Projects | Unreal modules used by `FlyingPresentation`. | UE 5.8 engine modules. | Engine modules. | Covered by applicable Unreal Engine terms. | Include with Unreal notice surface as required by Epic terms. |
| JSBSim | Optional CoreSim adapter for infrastructure testing and baseline FDM integration. | JSBSim 1.2.3, tag `v1.2.3`, commit `570e8115a102df8f877b11e0e59b964ea483e3c0`. | Not vendored. CMake uses installed files or fetches only when `FLYING_CORE_SIM_FETCH_JSBSIM=ON`. | LGPL-2.1-or-later per upstream project. | Preserve LGPL notice, source offer and dynamic/static-linking compliance evidence for any build that includes the adapter. |
| PROJ | Offline CRS transformation support for GIS and terrain processing. | 9.5.x intent. | Not vendored in this repository. | PROJ license and grid-data obligations must be recorded before release use. | Include exact installed version, license and grid-data notice in final release manifest if used. |
| Catch2 | Native unit and integration tests. | 3.7.x intent. | Not vendored here. | Preserve Catch2 license notice if vendored or redistributed. | Test-only; include only when redistributed in source or binary form. |

## Build, Signing And Packaging Tools

| Tool | Role | Version or bound | Distribution status | License or policy fact | Required release action |
| --- | --- | --- | --- | --- | --- |
| CMake | Native configuration and test target generation. | Minimum 3.25 in root `CMakeLists.txt`. | Build tool, not redistributed by the installer. | Tool license not bundled in this repository. | Record exact build host version in release manifest. |
| MSVC / Windows SDK | Win64 compiler, SDK and `signtool.exe`. | Windows SDK 10.0.26100.x intent for signing. | Build/signing tool, not redistributed. | Microsoft tool license applies. | Record SDK version and signing certificate custody evidence outside the repository. |
| Inno Setup | Installer generation using `packaging/FlyingInstaller.iss`. | Inno Setup 6. | Build tool, not redistributed except installer output. | Installer tooling license review required before release. | Record exact Inno Setup version in release manifest. |
| PowerShell packaging scripts | Build, signing, update and repair automation. | Repository-authored scripts under `packaging/`. | Redistributed with source release if included. | Same project license status as repository. | Keep scripts and generated manifests together in release evidence. |

## Data Packages

| Data family | Source status | License fact | Release attribution and traceability |
| --- | --- | --- | --- |
| CUZK DMR 5G | Allowed for Version 1 when sourced from official CUZK distribution channels and recorded in package manifests. | CC BY 4.0. | Must include CUZK attribution, source URL, change notice, checksums and package lineage. |
| CUZK DMP 1G | Allowed as auxiliary height source for object-height estimation, not primary physical relief. | CC BY 4.0. | Same CUZK attribution and change-notice requirements. |
| CUZK Ortofoto Ceske republiky | Allowed for offline imagery and visual measurement reference. | CC BY 4.0. | Same CUZK attribution and change-notice requirements. |
| CUZK ZABAGED polohopis | Allowed for roads, railways, water, built-up areas, vegetation, map layers and object placement. | CC BY 4.0. | Same CUZK attribution and change-notice requirements. |
| CUZK Geonames | Allowed for offline place names and map labels. | CC BY 4.0. | Same CUZK attribution and change-notice requirements. |
| AIM/AIP/VFR data | Blocked for automated import and redistribution without written permission. | No redistribution license is recorded in this repository. | Do not include AIM/AIP/VFR content unless written permission and notices are archived. |
| Operator-provided airport/runway/SLZ data | Allowed only when written confirmations/source files and permitted uses are archived. | Per operator grant. | Release packages must identify the operator source, reviewer, date, uncertainty and approval state. |
| Project-derived airport/runway/obstacle data | Allowed when derived from approved sources and labeled as derived. | Project-authored data plus source-data obligations. | Do not mark as validated until source and uncertainty review passes. |
| Flying Trainer One aircraft data | Project-authored unbranded trainer model. | CC0-1.0 project-authored data package notice in `core_sim/aircraft/flying_trainer_one/aircraft-config.json`. | Must retain no-manufacturer-endorsement and no type-fidelity notice. |

## Required CUZK Attribution

English:

> Contains information from the Czech Office for Surveying, Mapping and Cadastre (CUZK): DMR 5G, DMP 1G, Ortofoto Ceske republiky, ZABAGED and Geonames, licensed under Creative Commons Attribution 4.0 International (CC BY 4.0), https://creativecommons.org/licenses/by/4.0/. Source: https://geoportal.cuzk.cz/. Data were transformed, tiled, generalized and/or otherwise adapted for the Flying simulator. CUZK does not endorse this product.

Czech:

> Obsahuje informace Ceskeho uradu zememerickeho a katastralniho (CUZK): DMR 5G, DMP 1G, Ortofoto Ceske republiky, ZABAGED a Geonames, poskytnute pod licenci Creative Commons Attribution 4.0 International (CC BY 4.0), https://creativecommons.org/licenses/by/4.0/. Zdroj: https://geoportal.cuzk.cz/. Data byla transformovana, dlazdicovana, generalizovana a/nebo jinak upravena pro simulator Flying. CUZK tento produkt neschvaluje ani nepodporuje.

The legal-data gate keeps the canonical diacritic form. ASCII text here is acceptable for packaging surfaces that cannot guarantee Czech character rendering, but user documentation should prefer the canonical text from `docs/governance/m0-legal-data-gate.md`.

## In-App Attribution Review

The offline navigation map package manifest generated by `data_pipeline/src/pilot_region_packages.cpp` records `attribution.visible: true`, per-layer attribution strings and `navigationMap.attributionVisible: true`. `UFlyingOfflineNavigationMapWidget` refuses manifests that omit attribution and paints the loaded attribution string in the map paint path. Runtime terrain and map package manifests remain local files installed with the product, so users can inspect source lineage and license metadata without network access.
