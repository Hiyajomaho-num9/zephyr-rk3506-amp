# Zephyr Official Doc Audit Summary

> Historical documentation inventory. Paths, counts, and TSV hashes describe
> that snapshot; the live documentation is now the SDK-local `zephyr/doc/`.
> See [BSP-CHECKLIST.md](../../../../docs/rk3506/BSP-CHECKLIST.md) for current validation.

## Scope

Source tree:

```text
/home/kuro/zephyr-work/zephyr/doc
```

This audit is the control point for the RK3506 Zephyr BSP cleanup. It makes the
official docs explicit before more BSP changes are made.

## Inventory status

Generated artifacts:

- `zephyr-doc-inventory.tsv`
  - all files under Zephyr `doc/`
  - fields: `path`, `top_level`, `bytes`, `sha256`, `kind`
- `zephyr-doc-heading-index.tsv`
  - every text-like doc file (`.rst`, `.md`, `.txt`, `.dox`, `.in`)
  - fields: `path`, `top_level`, `first_nonempty_line`, `headings_sample`

Counts from this checkout:

| Kind | Count |
| --- | ---: |
| All doc files | 1072 |
| Text-like docs | 764 |
| Non-text assets/scripts/templates | 308 |

Top-level distribution:

| Top level | Files | Text | Assets | Bytes |
| --- | ---: | ---: | ---: | ---: |
| connectivity | 301 | 217 | 84 | 9471378 |
| services | 172 | 120 | 52 | 4170900 |
| develop | 141 | 111 | 30 | 1871974 |
| hardware | 119 | 104 | 15 | 1113554 |
| kernel | 75 | 60 | 15 | 1246065 |
| build | 63 | 37 | 26 | 1586048 |
| releases | 43 | 43 | 0 | 2595297 |
| _extensions | 29 | 0 | 29 | 265758 |
| contribute | 29 | 27 | 2 | 281318 |
| project | 18 | 11 | 7 | 418680 |
| _static | 16 | 0 | 16 | 94839 |
| security | 15 | 12 | 3 | 350521 |
| root files | 14 | 11 | 3 | 268494 |
| _doxygen | 11 | 2 | 9 | 105619 |
| safety | 10 | 8 | 2 | 116104 |
| _templates | 7 | 0 | 7 | 10133 |
| _scripts | 5 | 0 | 5 | 82143 |
| templates | 2 | 0 | 2 | 2327 |
| images | 1 | 0 | 1 | 112967 |
| introduction | 1 | 1 | 0 | 7759 |

## Reading policy

This artifact does not pretend every page has already been semantically
reviewed. It establishes complete coverage first:

1. Every doc file is inventoried and checksumed.
2. Every text-like doc has a heading-level routing index.
3. BSP-relevant docs are read in detail and summarized in the Trellis Zephyr
   spec.
4. Non-BSP buckets remain indexed and can be promoted to detailed reading when
   a code change touches their domain.

## BSP-critical docs already promoted to detailed review

| Area | Official docs |
| --- | --- |
| Board porting | `hardware/porting/board_porting.rst` |
| SoC porting | `hardware/porting/soc_porting.rst` |
| Application roots | `develop/application/index.rst` |
| Zephyr modules | `develop/modules.rst` |
| Devicetree vs Kconfig | `build/dts/dt-vs-kconfig.rst` |
| Devicetree bindings | `build/dts/bindings-intro.rst`, `build/dts/bindings-upstream.rst` |
| Kconfig style | `contribute/style/kconfig.rst` |
| Devicetree style | `contribute/style/devicetree.rst` |
| CMake style | `contribute/style/cmake.rst` |
| Device model | `kernel/drivers/index.rst` |
| MBOX API | `hardware/peripherals/mbox.rst` |
| Cache/coherency | `hardware/cache/guide.rst`, `hardware/cache/config.rst` |
| IPC service | `services/ipc/index.rst`, `services/ipc/ipc_service/ipc_service.rst` |
| Twister/test metadata | `develop/test/twister.rst`, `develop/test/index.rst` |

## Full top-level bucket routing

The workflow and skill intentionally cover every major Zephyr doc bucket:

| Bucket | Status | Routing note |
| --- | --- | --- |
| `build/` | indexed; BSP-critical subset promoted | Build system, Kconfig, DTS, sysbuild, snippets, signing/flashing. |
| `contribute/` | indexed; style subset promoted | Coding/style/doc/upstream contribution rules. |
| `develop/` | indexed; app/module/test subset promoted | Application layout, modules, west, tests, debug, toolchains. |
| `hardware/` | indexed; board/SoC/cache/mbox subset promoted | Board, SoC, peripherals, cache/MMU, architecture, pinctrl. |
| `kernel/` | indexed; driver model promoted | Kernel services, device model, timing, memory, userspace. |
| `project/` | indexed; on-demand | Governance and project process for upstream planning. |
| `releases/` | indexed; on-demand | Release notes and migration guides, especially for Zephyr 4.4 changes. |
| `safety/` | indexed; on-demand | Safety process and long-run/fault-policy implications. |
| `security/` | indexed; on-demand | Security process, trust boundaries, vulnerability handling. |
| `services/` | indexed; IPC subset promoted | IPC, shell, logging, watchdog, PM, storage, zbus, tracing, etc. |
| `connectivity/` | indexed; on-demand | CAN, Modbus, networking, USB, Bluetooth, LoRa. |

## Current RK3506 direction from docs

- Keep `zephyr-rk3506` as an out-of-tree Zephyr module-style repository.
- `zephyr/module.yml` declares `board_root`, `soc_root`, and `dts_root`; the
  remaining per-app root settings are SDK direct-build compatibility glue.
- Keep board support under `boards/<vendor>/<board>`.
- Keep SoC support under `soc/<vendor>/<soc>`.
- Keep the RK3506 SoC include under `dts/arm/rockchip/rk3506.dtsi`.
- Keep RK3506-specific compatibles covered by bindings under `dts/bindings/...`.
- Treat shared memory/cache policy as hardware/devicetree plus SoC code, not
  application-only convention.
- Keep validation-only RPMsg/cache stress code in tests; keep normal pingpong
  sample quiet and production-like.

## Metadata audit result

Detailed pass:

- `RK3506_BSP_METADATA_AUDIT.md`

Current result:

- Board platform is `ok3506b_s12_amp_uart1`; HWMv2 qualifier metadata reports
  `rk3506`.
- Board directory is `boards/forlinx/ok3506b_s12/`; `forlinx` is provided by
  the local `dts/bindings/vendor-prefixes.txt` in the RK3506 DTS root.
- `SOC_RK3506` selects `SOC_SERIES_RK3506`, which selects upstream
  `SOC_FAMILY_ROCKCHIP`.
- RK3506 sample/test metadata is build-only for Twister because runtime depends
  on the OK3506 Linux AMP host and RPMsg char-device flow.
- The production RPMsg sample now has `README.rst`.

Phase 6.6 update:

- public RK3506 feature Kconfig symbols now use `SOC_RK3506_*`;
- mailbox/RPMsg glue is documented as RK3506 SoC platform glue for the
  out-of-tree BSP;
- production sample source is split from validation probe/thrash source.

Deferred before upstream submission:

- decide whether mailbox/RPMsg glue later graduates to Zephyr MBOX/IPC-service
  APIs
- decide whether CPU2 is modeled as a SoC CPU cluster rather than a board
  purpose suffix
- replace or isolate Rockchip SDK image packaging from any upstream Zephyr
  build/flash path
