# settings_providers tests

* `run.sh` — host self-test (g++, no Genode runtime): the production
  validators in `../common/sp_validate.h` and the row helpers in
  `../common/sp_rows.h`, run through `self_test.cc` /
  `sp_logic_tests.cc` (the sponge_configd self_test precedent).
  Expect `self-test: PASS (N checks)`.
* `structural_checks.sh` — pure structural evidence: per-provider
  target.mk/main.cc/README.md presence, TARGET names, config-channel
  and quick-pair labels pinned against
  `sponge_configd/key_domain_acl.cc`, forbidden universal-writer
  references, the display/terminal typed-key seam markers, and the
  pkg metadata declarations. Expect `structural checks: PASS`.

Neither script is part of the Genode build (absent from every
target.mk).
