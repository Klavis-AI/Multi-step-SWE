#!/bin/bash
# Build the deterministic fixture site and the client-side work trees.
#
#   mksite.sh <site-dir> <work-dir> <keys-dir> [main|alt|ssl]
#
# Frozen clock, fixed identities, pre-generated keys, so every object id is
# stable. Running this twice must produce byte-identical repositories; the
# recorder checks that.
set -euo pipefail
SITE="${1:?site dir}"
WORK="${2:?work dir}"
KEYS="${3:?keys dir}"
PROFILE="${4:-main}"       # main | alt | ssl -- same fixtures, another config

export GIT_CONFIG_GLOBAL=/tmp/mgs-gitconfig.global
export GIT_CONFIG_SYSTEM=/dev/null
export GIT_CONFIG_NOSYSTEM=1
cat > "$GIT_CONFIG_GLOBAL" <<'EOF'
[user]
	name = Fixture Author
	email = fixture@example.invalid
[init]
	defaultBranch = master
[core]
	abbrev = 40
	autocrlf = false
	quotePath = true
[gc]
	auto = 0
[advice]
	detachedHead = false
[pack]
	threads = 1
EOF

FIXED_DATE="2001-02-03T04:05:06+0000"
export GIT_AUTHOR_DATE="$FIXED_DATE" GIT_COMMITTER_DATE="$FIXED_DATE"
export GIT_AUTHOR_NAME="Fixture Author" GIT_COMMITTER_NAME="Fixture Author"
export GIT_AUTHOR_EMAIL="fixture@example.invalid" GIT_COMMITTER_EMAIL="fixture@example.invalid"
export TZ=UTC
export LC_ALL=C.UTF-8

rm -rf "$SITE" "$WORK"
mkdir -p "$SITE/etc/users/tester" "$SITE/etc/users/second" "$SITE/repos" "$SITE/logs" "$WORK"

# ---------------------------------------------------------------------------
# The whole hook list, in the codebase's own config file and its own format.
# ---------------------------------------------------------------------------
# Every key here is one this codebase actually reads. The values exercise the
# parsers as well as the settings: "2 min" and "5 min" go through
# ConfigUtil.getTimeUnit, the sshd.cipher list goes through SshDaemon.filter's
# four branches (a bare name clears the defaults, "+" adds, "-" removes, an
# unknown name is reported), and two listenAddress entries go through
# SocketUtil. The client forces aes128-cbc and hmac-sha1, so the negotiated
# algorithms stay inside the list this config leaves enabled.
if [ "$PROFILE" = alt ]; then
  # The same application over a second configuration: no canonicalWebUrl (so it
  # is derived from the request), an absolute basePath, a single listen address,
  # maxConnectionsPerUser disabled, and the durations spelled in other units.
  cat > "$SITE/etc/gerrit.config" <<EOF
[gerrit]
	basePath = $SITE/repos
[sshd]
	listenAddress = 127.0.0.1:29428
	# An empty host means the wildcard, which is a different branch of
	# SocketUtil.parse from the "*" the primary profile spells out.
	listenAddress = :29429
	# A name that does not resolve. SshDaemon logs it and carries on with the
	# addresses that did parse, so the observable consequence of losing that
	# handling is that this instance stops answering at all.
	listenAddress = nosuchhost.invalid:29431
	maxAuthTries = 2
	maxConnectionsPerUser = 0
	loginGraceTime = 90 s
	tcpkeepalive = false
	# reuseaddress stays true here: the two recording passes rebind this port
	# seconds apart, and the false value only changes a socket option, not a
	# branch that cfg.getBoolean does not already cover.
	reuseaddress = true
[transfer]
	timeout = 30 sec
[cache "projects"]
	maxAge = 1 h
[user]
	name = Alt Fixture Server
EOF
elif [ "$PROFILE" = ssl ]; then
  # A canonicalWebUrl that starts with "https:" and nothing else of interest.
  # That one prefix is what makes WebModule install RequireSslFilter over "/*",
  # so this instance answers every request with a protocol redirect and never
  # reaches the servlets at all. No other profile runs that filter.
  cat > "$SITE/etc/gerrit.config" <<EOF
[gerrit]
	basePath = $SITE/repos
	canonicalWebUrl = https://git.example.invalid/
[sshd]
	listenAddress = 127.0.0.1:29438
	listenAddress = 127.0.0.1:29439
[user]
	name = Ssl Fixture Server
EOF
else
cat > "$SITE/etc/gerrit.config" <<'EOF'
[gerrit]
	basePath = repos
	canonicalWebUrl = http://127.0.0.1:8080/
[sshd]
	listenAddress = *:29418
	listenAddress = 127.0.0.1:29419
	# Two spellings SocketUtil.parse must reject rather than resolve: a bracket
	# that never closes and a port that is not a number. Both are reported and
	# skipped, so the instance still comes up on the two addresses above.
	listenAddress = [::1
	listenAddress = 127.0.0.1:notaport
	maxAuthTries = 4
	maxConnectionsPerUser = 16
	loginGraceTime = 2 min
	threads = 4
	batchThreads = 2
	streamThreads = 3
	tcpkeepalive = true
	reuseaddress = true
	cipher = aes128-cbc
	cipher = +aes256-cbc
	cipher = -blowfish-cbc
	cipher = nosuchcipher
	mac = +hmac-sha1
	mac = -hmac-md5-96
[transfer]
	timeout = 5 min
[cache]
	directory = cache
[cache "projects"]
	memoryLimit = 512
	maxAge = 30 min
[user]
	name = Fixture Server
EOF
fi

install -m 600 "$KEYS/ssh_host_rsa_key" "$SITE/etc/ssh_host_rsa_key"
install -m 644 "$KEYS/tester.pub" "$SITE/etc/users/tester/id_rsa.pub"
install -m 644 "$KEYS/second.pub" "$SITE/etc/users/second/id_rsa.pub"

if [ "$PROFILE" = alt ]; then
  # A second host key, so this instance advertises two and HostKeyProvider
  # takes the branch that collects both standard files rather than the one.
  # Fixed key material, so the advertised fingerprints are fixtures.
  #
  # The branch under test is "both standard host-key files exist", not "a DSA
  # key exists": HostKeyProvider hard-codes the two filenames and passes every
  # one that exists to FileKeyPairProvider, and the driver never offers
  # ssh-dss, so nothing observable here is DSA. The file keeps the name the
  # application looks for and holds a second RSA key: the filename is a lie
  # this fixture tells on purpose, because the application hard-codes it and
  # real DSA material is refused outright by every SSH implementation current
  # enough to be on this classpath.
  install -m 600 "$KEYS/ssh_host_dsa_key" "$SITE/etc/ssh_host_dsa_key"
elif [ "$PROFILE" = main ]; then
  # secure.config exists here and not in the alt profile: the config provider
  # layers it under gerrit.config when the file is present, and that layering
  # is a branch of startup rather than a setting anything reads.
  cat > "$SITE/etc/secure.config" <<'EOF'
[gerrit]
	# Nothing in this codebase reads a secret. The file is here so the
	# layered-config path runs; the value is inert on purpose.
	secureConfigPresent = true
EOF
  chmod 600 "$SITE/etc/secure.config"
fi

# Authentication fixtures for the three ways ToyPubKeyAuth can fail to find a
# usable key, alongside the two that work.
#   tester   -- a good key plus an unparsable one in the same directory
#   broken   -- only an unparsable key                (parse throws)
#   empty    -- a directory with no *.pub at all      (no candidates)
#   notdir   -- a regular file where a directory is expected
printf 'ssh-rsa NOT-BASE64!!! broken@example.invalid\n' > "$SITE/etc/users/tester/zz-broken.pub"
mkdir -p "$SITE/etc/users/broken" "$SITE/etc/users/empty"
printf 'ssh-rsa NOT-BASE64!!! broken@example.invalid\n' > "$SITE/etc/users/broken/id_rsa.pub"
printf 'not a key directory\n' > "$SITE/etc/users/notdir"

q() { git "$@" >/dev/null 2>&1; }

# ---------------------------------------------------------------------------
# demo.git -- the main fixture: two commits, a second branch, both tag kinds,
# a nested tree, a tag-of-a-tag.
# ---------------------------------------------------------------------------
git init -q "$WORK/demo"; cd "$WORK/demo"
printf 'hello\n' > a.txt
q add a.txt; q commit -m "first commit"
printf 'hello\nworld\n' > a.txt
mkdir -p sub; printf 'nested\n' > sub/b.txt
q add -A; q commit -m "second commit"
q tag v1.0
q tag -a v1.1 -m "annotated tag"
q tag -a v1.2 -m "tag of a tag" v1.1
q checkout -b topic
printf 'topic\n' > c.txt
q add c.txt; q commit -m "topic commit"
q checkout master
q clone --bare "$WORK/demo" "$SITE/repos/demo.git"

# ---------------------------------------------------------------------------
# empty.git -- bare, zero refs. Advertisement edge case.
# ---------------------------------------------------------------------------
git init -q --bare "$SITE/repos/empty.git"

# ---------------------------------------------------------------------------
# unicode.git -- non-ASCII ref names and paths, a NUL-bearing blob, a 180-char
# ref, a commit message with a non-ASCII body and one with a CR.
# ---------------------------------------------------------------------------
git init -q "$WORK/unicode"; cd "$WORK/unicode"
printf 'caf\xc3\xa9\n' > $'caf\xc3\xa9.txt'
printf '\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\n' > japanese.txt
printf 'a\x00b\n' > with-nul.bin
printf 'x' > $'sp ace.txt'
q add -A; q commit -m $'unicode content \xc3\xa9\xc3\xa8'
printf 'y\n' >> japanese.txt
q add -A; q commit -m $'crlf subject\r\n\r\nbody with \r in it\r'
q branch $'feature/\xc3\xa9t\xc3\xa9'
q branch "long/$(python3 -I -c 'print("x"*180)')"
q branch 'has.dot'
q clone --bare "$WORK/unicode" "$SITE/repos/unicode.git"

# ---------------------------------------------------------------------------
# many.git -- 301 refs, packed. Large advertisement.
# ---------------------------------------------------------------------------
git init -q "$WORK/many"; cd "$WORK/many"
printf 'base\n' > base.txt
q add base.txt; q commit -m "base"
for i in $(seq -w 1 300); do q branch "b$i"; done
q clone --bare "$WORK/many" "$SITE/repos/many.git"
q --git-dir="$SITE/repos/many.git" pack-refs --all

# ---------------------------------------------------------------------------
# group/inner.git -- a nested project path.
# ---------------------------------------------------------------------------
mkdir -p "$SITE/repos/group"
git init -q "$WORK/inner"; cd "$WORK/inner"
printf 'inner\n' > i.txt
q add i.txt; q commit -m "inner"
q clone --bare "$WORK/inner" "$SITE/repos/group/inner.git"

# ---------------------------------------------------------------------------
# big.git -- one 1 MiB incompressible blob plus a gitlink and an empty tree,
# so pack generation and tree walking see more than tiny objects.
# ---------------------------------------------------------------------------
git init -q "$WORK/big"; cd "$WORK/big"
python3 -I - <<'PY'
import random
random.seed(20010203)
with open('blob.bin','wb') as f:
    f.write(bytes(random.getrandbits(8) for _ in range(1024*1024)))
PY
printf 'small\n' > small.txt
q add -A; q commit -m "big blob"
# a gitlink entry (submodule-shaped tree entry) with no .gitmodules
GL=$(git --git-dir="$SITE/repos/demo.git" rev-parse refs/heads/master)
git update-index --add --cacheinfo "160000,$GL,modlink"
q commit -m "add gitlink"
# a zero-byte file and a deep path, so the tree walk sees more than one level.
# (Deliberately NOT the empty tree 4b825dc6...: git treats that id as always
# known, but it is not an object in the pack, so serving it is a property of
# the git library rather than of this application.)
: > zero.txt
mkdir -p d1/d2/d3/d4/d5; printf 'deep\n' > d1/d2/d3/d4/d5/leaf.txt
q add -A; q commit -m "zero byte file and deep path"
q branch deep
q clone --bare "$WORK/big" "$SITE/repos/big.git"

# ---------------------------------------------------------------------------
# head-dangling.git -- HEAD symref to a branch that does not exist.
# ---------------------------------------------------------------------------
git init -q --bare "$SITE/repos/head-dangling.git"
q --git-dir="$SITE/repos/head-dangling.git" symbolic-ref HEAD refs/heads/nothere

# ---------------------------------------------------------------------------
# notrepo/ -- a plain directory under basePath that is not a repository.
# ---------------------------------------------------------------------------
mkdir -p "$SITE/repos/notrepo"
printf 'not a repo\n' > "$SITE/repos/notrepo/README"

# ---------------------------------------------------------------------------
# back\slash.git -- a repository whose directory name contains a backslash.
#
# LocalDiskRepositoryManager refuses names containing '\' before it ever looks
# at the disk. Without a repository that only that guard is hiding, deleting
# the guard changes nothing anyone can see and the check is untested. The name
# is unreachable over HTTP, where the container normalises the path, and
# reachable over SSH, where the command line arrives verbatim.
# ---------------------------------------------------------------------------
q clone --bare "$WORK/inner" "$SITE/repos/back\\slash.git"

# ---------------------------------------------------------------------------
# zipped.git.zip -- a repository that does not exist until the server starts.
#
# LocalDiskRepositoryManager's constructor unzips every *.zip under basePath
# into a sibling directory, deleting that directory first, so this fixture is
# rebuilt on every boot and its contents are whatever the archive says. Stored
# uncompressed with a fixed timestamp, so the archive bytes are stable.
# ---------------------------------------------------------------------------
python3 -I - "$SITE" <<'PY'
import os
import sys
import zipfile

site = sys.argv[1]
src = os.path.join(site, "repos", "group", "inner.git")
out = os.path.join(site, "repos", "zipped.git.zip")
dirs_, files_ = [], []
for root, ds, fs in os.walk(src):
    for d in ds:
        dirs_.append(os.path.relpath(os.path.join(root, d), src) + "/")
    for f in fs:
        p = os.path.join(root, f)
        files_.append((os.path.relpath(p, src), p))
dirs_.sort()
files_.sort()
with zipfile.ZipFile(out, "w", zipfile.ZIP_STORED) as z:
    # Directory entries, not only files: a bare repository's refs/ is empty
    # once the refs are packed, and JGit does not recognise a directory as a
    # repository unless refs/ is there. An archive of files alone unpacks into
    # something the server then refuses to serve.
    for rel in dirs_:
        info = zipfile.ZipInfo(rel, date_time=(2001, 2, 3, 4, 5, 6))
        info.external_attr = (0o755 << 16) | 0x10
        z.writestr(info, b"")
    for rel, p in files_:
        info = zipfile.ZipInfo(rel, date_time=(2001, 2, 3, 4, 5, 6))
        info.external_attr = 0o644 << 16
        with open(p, "rb") as fh:
            z.writestr(info, fh.read())
PY

# ---------------------------------------------------------------------------
# Push targets.
# ---------------------------------------------------------------------------
q clone --bare "$WORK/demo" "$SITE/repos/writable.git"
q clone --bare "$WORK/demo" "$SITE/repos/writable2.git"

# ---------------------------------------------------------------------------
# A pristine template of every repository, outside basePath so it is never
# served. The driver restores from here around every mutating case, so a case
# cannot see another case's writes and the suite is order-independent. This
# codebase has no write authorisation beyond the SSH key, so a push aimed at a
# read-only fixture really does land -- the restore is what keeps that honest
# rather than destructive.
# ---------------------------------------------------------------------------
mkdir -p "$SITE/templates"
(cd "$SITE/repos" && find . -maxdepth 2 -type d -name '*.git' -prune -print) \
  | sed 's|^\./||' | while read -r rel; do
      mkdir -p "$SITE/templates/$(dirname "$rel")"
      cp -a "$SITE/repos/$rel" "$SITE/templates/$rel"
    done

# ---------------------------------------------------------------------------
# pusher/ -- the client-side source of every push. Fixed commits so the pushed
# object ids are stable.
# ---------------------------------------------------------------------------
q clone "$WORK/demo" "$WORK/pusher"
cd "$WORK/pusher"
q config user.name "Fixture Author"
q config user.email fixture@example.invalid
# newbranch forks from the commit BEFORE the served tip, so pushing it onto
# master is a genuine non-fast-forward.
q checkout -b newbranch origin/master~1
printf 'divergent\n' > d.txt
q add -A; q commit -m "divergent commit"
q tag -a pushtag -m "pushed annotated tag"
q tag pushlight
# ff1 and ff2 descend from the served tip, so they are fast-forwards.
q checkout master
printf 'hello\nworld\nff1\n' > a.txt
q add -A; q commit -m "ff1"
q branch ff1
printf 'hello\nworld\nff1\nff2\n' > a.txt
q add -A; q commit -m "ff2"
q branch ff2
# A ref name the server side must accept verbatim.
q branch $'push/\xc3\xa9t\xc3\xa9' ff1
q checkout -q master

cd /
sync
echo "SITE READY: $SITE"
echo "WORK READY: $WORK"
