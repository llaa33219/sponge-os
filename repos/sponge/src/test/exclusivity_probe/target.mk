# exclusivity_probe — exclusive-group placement regression verifier.
#
# Proves the install pre-flight refuses a conflicting exclusive-group
# claim on FIRST execution (leaving no state behind) and that a
# byte-identical duplicate request replays the cached refusal without
# re-running policy. Used by run/sponge-pkg-exclusive.run.

TARGET   := exclusivity_probe
SRC_CC   := main.cc
LIBS     := base
