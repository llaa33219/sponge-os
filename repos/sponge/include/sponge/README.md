# Sponge OS shared headers

Headers in this directory are shared by multiple Sponge OS components.
The Genode build system includes them via `INC_DIR += $(REP_DIR)/include`.

## Current headers

- `sponge/version.h` — Sponge OS version information (single source).

## Additional rules

- Every header is guarded with `#pragma once`.
- Every symbol lives in the `Sponge::` namespace (or a sub-namespace).
- Backend RPC interface definitions live here (planned).