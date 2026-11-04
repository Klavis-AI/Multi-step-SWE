FROM debian:bookworm-slim

# Build toolchain for FFmpeg's libswscale plus the assembler its kernels need,
# and the measurement tooling the verifier and the candidate share.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash binutils ca-certificates ccache coreutils diffutils file gawk gcc \
       g++ git make nasm patch procps python3 time util-linux \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY timer.sh /app/timer.sh
COPY build.sh /app/build.sh
COPY tools/ /app/tools/
COPY vendor/ /tmp/vendor/
RUN chmod +x /app/timer.sh /app/build.sh /app/tools/build_measure.sh

# Base repository for milestone 1: upstream FFmpeg at the pinned release with
# this ladder's six stages removed from libswscale/x86, from every other
# architecture's implementation of the same stages, and from the one copy that
# lives outside libswscale. Everything else is upstream and intact -- the build
# system, the checkasm differential suite, and the portable C implementation of
# every stage, which is what makes the tree complete and correct while it is
# slow.
#
# The dispatch entry points stay. A level simply does not assign the fields
# whose kernels are absent, which is exactly the shape upstream's own
# --disable-x86asm build has; nothing in the tree says a stage was removed.
RUN . /tmp/vendor/PIN \
    && git clone --quiet --branch "$FFMPEG_TAG" --depth 1 "$FFMPEG_REPO" /app/ffmpeg \
    && cd /app/ffmpeg \
    && test "$(git rev-parse HEAD)" = "$FFMPEG_COMMIT" \
    && rm -rf .git \
    && python3 /tmp/vendor/strip.py /app/ffmpeg in rgb ops hs out yuv2rgb

# Build it once here, so the image ships a warm object cache and the candidate
# never waits for a cold compile of two hundred files it did not touch.
RUN /app/build.sh > /tmp/seedbuild.log 2>&1 || { tail -30 /tmp/seedbuild.log; exit 1; }

# strip.py names every stage of the ladder and PIN names the commit whose code
# is the answer, so neither reaches the candidate. The upstream tree itself
# does: the scored target is published on purpose -- checkasm randomizes the
# inputs, so there is no stable configuration to learn, and the measured set is
# listed in the milestone's interface file.
RUN rm -rf /tmp/vendor /tmp/seedbuild.log
