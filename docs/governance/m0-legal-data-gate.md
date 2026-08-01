# M0 Legal And Data-Source Gate

Document ID: DR-M0-LEGAL-DATA  
Version: 1.0.0  
Status: Conditional - ČÚZK open-data path allowed; AIM/AIP/VFR import and redistribution blocked until written permission or fallback evidence is complete  
Effective date: 2026-08-01  
Owner: Project governance  
Legal note: This is an engineering governance record, not legal advice.

## Decision

Version 1 may use listed ČÚZK open datasets as offline source data under Creative Commons Attribution 4.0 International (CC BY 4.0), provided attribution, source metadata, change notices and redistribution constraints in this document are followed.

Version 1 must not scrape, import, embed, redistribute or package AIP ČR, VFR příručka ČR, AIM web content, AIM terrain datasets or AIM obstacle datasets unless the project obtains prior written permission from Řízení letového provozu ČR, s.p. / AIM or another explicit license covering the intended automated processing and product redistribution.

Until that permission exists, the approved fallback process is to build airport, runway, SLZ and obstacle data from:

- operator-provided written confirmations and source files;
- direct geodetic survey commissioned or owned by the project;
- manual measurement and derivation from allowed ČÚZK open data, with every derived value labeled as derived;
- public official registers used only as completeness checklists where their terms permit that use;
- manual validation records that identify reviewer, date, source, uncertainty and approval state.

No fallback-derived aerodrome, runway, SLZ area, surface, light, obstacle or distance may be marked `validated` until its source and uncertainty record passes project review.

## Allowed ČÚZK Datasets

The following datasets are allowed for Version 1 planning and later pipeline work only when sourced from official ČÚZK distribution channels and recorded in a data manifest:

| Dataset | Intended Version 1 use | Gate decision |
| --- | --- | --- |
| DMR 5G | Primary bare-earth terrain height source for the Czech Republic. | Allowed under CC BY 4.0 with attribution and source metadata. |
| DMP 1G | Auxiliary source for building, vegetation and obstacle-height estimation; not the primary physical relief model. | Allowed under CC BY 4.0 with attribution and source metadata. |
| Ortofoto České republiky | Offline imagery color source and visual measurement reference. | Allowed under CC BY 4.0 with attribution, source metadata and change notices. |
| ZABAGED polohopis | Offline vector features for roads, railways, water, built-up areas, vegetation areas and map layers. | Allowed under CC BY 4.0 with attribution and source metadata. |
| Geonames | Offline place names and map labels. | Allowed under CC BY 4.0 with attribution and source metadata. |

ČÚZK source packages must be stored outside the runtime repository unless a later data-management record explicitly approves their storage location. Derived terrain, imagery and vector packages must record source dataset name, product version or update date when available, download URL, processing version, checksums and license.

## Attribution Requirements

Attribution must appear at minimum in:

- application credits or legal notices;
- user documentation;
- release notes or third-party notices for builds that contain derived ČÚZK data;
- each data package manifest in machine-readable form.

Required English attribution text:

> Contains information from the Czech Office for Surveying, Mapping and Cadastre (ČÚZK): DMR 5G, DMP 1G, Ortofoto České republiky, ZABAGED and Geonames, licensed under Creative Commons Attribution 4.0 International (CC BY 4.0), https://creativecommons.org/licenses/by/4.0/. Source: https://geoportal.cuzk.cz/. Data were transformed, tiled, generalized and/or otherwise adapted for the Flying simulator. ČÚZK does not endorse this product.

Required Czech attribution text:

> Obsahuje informace Českého úřadu zeměměřického a katastrálního (ČÚZK): DMR 5G, DMP 1G, Ortofoto České republiky, ZABAGED a Geonames, poskytnuté pod licencí Creative Commons Attribution 4.0 International (CC BY 4.0), https://creativecommons.org/licenses/by/4.0/. Zdroj: https://geoportal.cuzk.cz/. Data byla transformována, dlaždicována, generalizována a/nebo jinak upravena pro simulátor Flying. ČÚZK tento produkt neschvaluje ani nepodporuje.

If a build uses only a subset of the listed datasets, the attribution may list only the datasets actually included, but it must still identify ČÚZK, the CC BY 4.0 license URL, the source URL and that changes were made.

## Redistribution Constraints

- Do not apply product DRM, license terms, encryption or technical measures to ČÚZK-derived data packages in a way that prevents recipients from exercising CC BY 4.0 rights in those data components.
- Keep ČÚZK-derived package manifests accessible with the installed product.
- Do not imply ČÚZK endorsement, certification or validation of Flying.
- Preserve third-party license notices for Unreal Engine, Cesium for Unreal, JSBSim and any future libraries.
- Do not redistribute AIM/AIP/VFR content, AIM terrain datasets or AIM obstacle datasets without written permission that covers redistribution as part of a simulator product.
- Do not use external map APIs, public tile servers or online aeronautical services as required runtime dependencies for normal Version 1 flight.
- Do not include personal data in telemetry, package metadata or attribution records unless a separate privacy review approves it.

## AIM, AIP, VFR And Airport Data Status

Current gate status:

- AIM/AIP/VFR automated import: Blocked pending written permission.
- AIM/AIP/VFR redistribution: Blocked pending written permission.
- AIM terrain and obstacle datasets: Blocked pending written permission or explicit order/license terms covering simulator redistribution.
- VFR Manual SLZ content: Blocked for import and redistribution pending written permission.
- AIP airport data: Blocked for import and redistribution pending written permission.
- ÚCL Evidence letišť: May be used as a public official completeness checklist only after terms and allowed-use review; it is not approved as a standalone geometry source by this document.

Written permission must explicitly cover automated access if needed, extraction, transformation, internal QA storage, inclusion in offline product packages, redistribution to end users, update cadence, attribution text and any restrictions on commercial and non-commercial use.

## M0 Exit Rule

M0 may pass for data only if one of these conditions is true:

- Written AIM/AIP/VFR permission exists and is archived with the project governance records; or
- The fallback process has an approved master list proving all Version 1 airport, runway and SLZ coverage can be built without restricted AIM/AIP/VFR content.

Without one of those conditions, downstream importer, terrain package, airport package and release work remains No-Go.

## Sources Checked

Sources were checked for this governance record on 2026-08-01:

- ČÚZK open data overview: https://geoportal.cuzk.cz/Default.aspx?mode=TextMeta&text=data_uvod
- ČÚZK DMR 5G metadata: https://geoportal.cuzk.cz/Default.aspx?metadataID=CZ-CUZK-DMR5G-V&mode=TextMeta&side=vyskopis
- ČÚZK DMP 1G metadata: https://geoportal.cuzk.cz/Default.aspx?metadataID=CZ-CUZK-DMP1G-V&mode=TextMeta&side=vyskopis
- ČÚZK Ortofoto ČR metadata: https://geoportal.cuzk.cz/Default.aspx?metadataID=CZ-CUZK-ORTOFOTO-R&mode=TextMeta&side=ortofoto
- ČÚZK ZABAGED polohopis metadata: https://geoportal.cuzk.cz/Default.aspx?metadataID=CZ-CUZK-ZABAGED-VP&mode=TextMeta&side=zabaged
- ČÚZK Geonames metadata: https://geoportal.cuzk.cz/Default.aspx?metadataID=CZ-CUZK-GEONAMES-V&mode=TextMeta&side=Geonames
- Creative Commons BY 4.0 deed: https://creativecommons.org/licenses/by/4.0/
- AIM terms of use: https://aim.rlp.cz/?lang=cz&p=podminky-uziti
- AIM eAIP cover page: https://aim.rlp.cz/eaip/html/LK-cover-cz-CZ.html
- AIM VFR Manual introduction: https://aim.rlp.cz/vfrmanual/actual/gen_1_cz.html
- AIM terrain and obstacle datasets: https://aim.rlp.cz/?lang=cz&p=datasets
- ÚCL Evidence letišť: https://www.caa.gov.cz/letiste/evidence-letist/
