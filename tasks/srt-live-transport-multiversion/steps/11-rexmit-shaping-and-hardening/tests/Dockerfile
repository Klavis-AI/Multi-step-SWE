# Verifier image for step 11. Built with steps/11-rexmit-shaping-and-hardening/tests/ as the
# Docker build context: it cannot reach the task root, environment/, or another
# step, which is why helpers.sh and the driver sources are duplicated per step.
#
# The candidate's source arrives later through the declared /app artifact; it is
# never part of this image. The verifier configures, builds and installs it from
# scratch, then links its own drivers against that installation, so nothing the
# candidate leaves in a build tree can be mistaken for a graded result.
FROM ubuntu:20.04
ENV DEBIAN_FRONTEND=noninteractive

# Everything the candidate's CMake project and this step's drivers need. The
# list is identical in all eleven images so the layer is built once and shared,
# and it is a superset of environment/Dockerfile's list: a package that the
# agent's image has and this one lacks turns a correct solution into a build
# failure here, which is scored as every capability being absent.
#
# libssl-dev is not optional. Every release in this chain calls
# `find_package(OpenSSL REQUIRED)` unconditionally at the top of CMakeLists.txt
# -- encryption is not a switchable feature that can be configured away -- so
# without it the candidate's project does not configure at all.
RUN apt-get update -qq \
    && apt-get install -y -qq --no-install-recommends \
       bash build-essential ca-certificates cmake coreutils diffutils file \
       gawk grep libssl-dev pkg-config python3 sed tclsh \
    && rm -rf /var/lib/apt/lists/*

# Step identity, for the binding check at the top of test.sh.
#
# Written BEFORE the COPY below, so it names this IMAGE even when the copy is
# served from another step's cache entry. The builder identifies a build-context
# file by (relative path, size, mtime) and never reads its bytes, so two steps
# whose tests/ trees share a filename, a size and a second are indistinguishable
# to it -- and the wrong milestone's verifier gets baked in at an ordinary-looking
# score. Nothing inside a Dockerfile prevents that; this only reports it.
#
# Written as a LITERAL rather than as an ARG: harbor builds the verifier
# through docker-compose-build.yaml, which declares no `args:`, so no
# --build-arg can reach this build and an ARG default would be an override
# surface nothing can use.
#
# Keep this number, the directory's number and test.sh's check equal.
RUN echo "11" > /etc/harbor-step-id

COPY . /tests
RUN chmod +x /tests/test.sh

# Grading needs no network: the drivers call only the candidate's own library
# and the loopback interface, and every fixture is a literal inside a driver
# source file.
RUN mkdir -p /app /logs/verifier
WORKDIR /app
