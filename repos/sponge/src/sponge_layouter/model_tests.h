/* SPDX-License-Identifier: Apache-2.0
 *
 * Deterministic layout/state model cases for sponge_layouter
 * (docs/plans/phase16-sponge-wm.md D16W.5/D16W.1 + §11,
 * docs/plans/wm-state-table.md "Native WM Extension").
 *
 * One case list, two harnesses:
 *
 *   - self_test.cc — the host-side self-test (build/run command in
 *     its header comment) that proves the cases pass without a
 *     Genode runtime.
 *
 * The cases drive the PRODUCTION model (layout_model.{h,cc}) and the
 * production protocol code (<sponge/wm_protocol.h>): input is fed as
 * complete snapshots and gesture sequences, output is generated with
 * the production report generators and parsed back with the
 * production parsers. Expected tiling/chrome geometry is computed
 * with INDEPENDENT reference helpers transcribed from the committed
 * acceptance probes (test/native_wm_tiling_probe,
 * test/native_wm_common/native_wm_probe_util.h) so a model/probe
 * misunderstanding cannot self-certify. Negative controls (same-id
 * focus_request, sloppy button release, tiled drag, non-resizeable
 * resize, single sentinel coordinate, ...) assert that deliberate
 * wrong actions do NOT change state.
 */

#pragma once

namespace Sponge::Layout_model {

/*
 * Report callback: invoked once per check with the outcome and a
 * short description (with expected/got detail inline on failure).
 */
using Model_check_fn = void (*)(bool ok, char const *what);

/* Returns the number of failed checks. */
unsigned run_model_tests(Model_check_fn report);

}  /* namespace Sponge::Layout_model */
