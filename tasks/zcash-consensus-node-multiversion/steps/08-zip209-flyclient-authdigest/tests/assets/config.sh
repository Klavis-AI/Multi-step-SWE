# Per-step verifier configuration, sourced by test.sh.
STEP_NN=08
STEP_NAME=zip209-flyclient-authdigest
# Where the shared warm cargo cache lives inside the verifier image. Keeping
# the target directory off /app means a candidate's own `target/` never shadows
# it, and the artifact copied between steps stays small.
CARGO_TARGET=/cache/target
