# RK3506 historical engineering reports

Reviewed on 2026-10-06. These early reports retain their original paths,
phase labels, build commands, and validation claims for traceability.
They describe the checkout and hardware baseline at the time they were written.

Current SDK documentation:

- [Project status and next steps](../../../docs/rk3506/STATUS.md)
- [Build commands](../../../docs/rk3506/BUILD.md)
- [Applications, images, and test status](../../../docs/rk3506/testing/README.md)
- [Current BSP checklist](../../../docs/rk3506/BSP-CHECKLIST.md)

Report groups:

- `RK3506_ZEPHYR_AMP_RPMSG.md`: early AMP, RPMsg, and cache validation.
- `can/`: CAN bring-up, external reference plans, and driver rewrite notes.
- `official-doc-audit/`: earlier BSP metadata and upstream gap reports;
  the TSV files are inventories of that documentation snapshot.

The live Zephyr tree is now the real `zephyr/` directory inside the SDK.
Old `zephyr-work`, `zephyr-rk3506`, and external repository commands in
these reports are historical. Past hardware results do not certify the
2026-09-28 image or later source changes.
