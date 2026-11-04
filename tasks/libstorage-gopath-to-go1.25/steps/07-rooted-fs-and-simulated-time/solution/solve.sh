#!/usr/bin/env bash
# Reference solution, step 7: the vfs driver stops turning a caller's volume
# or snapshot id into a path and starts resolving it inside a root that the
# name cannot leave, and the key/value store stops keeping a goroutine alive
# to sweep its expired entries and starts forgetting them when it is asked.
#
# The shape of the change
# -----------------------
# Two defects and one rung, in five files.
#
#   * `go 1.25` is the rung. `os.Root` gained the methods this driver needs
#     -- `MkdirAll`, `RemoveAll`, `FS` -- at 1.25, and `testing/synctest`
#     became part of the language's own test surface at 1.25 rather than an
#     experiment behind a GOEXPERIMENT. Neither half of this milestone is
#     writable below it.
#
#   * The walk upwards. `drivers/storage/vfs/storage` formats a
#     caller-supplied id into its data directory -- `fmt.Sprintf("%s/%s.json",
#     d.volPath, volumeID)` -- and then opens, writes or deletes the result.
#     Formatting does not reject `..` and neither does `path.Join`, which
#     cleans it, so `../snap/vfs-000-001` is a volume id that names a
#     snapshot document and `../../outside` is a volume id that names a file
#     beside the whole data root. The only guard in front of those operations
#     is `gotil.FileExists`, which asks whether the *cleaned* path is there:
#     it says yes for the file one directory over and then the open, or the
#     delete, goes through. Four of those sites read; two of them write.
#
#     `os.Root` is the fix and it is a real one. The driver opens one root on
#     the volumes directory and one on the snapshots directory in `Init`, and
#     from then on every id is a *name* handed to the root that owns it. A
#     name that walks out is refused by the resolution itself, which is why
#     two roots rather than one: a single root on the data root would still
#     let a volume id reach `../snap`, because `snap` is inside it.
#
#     What must not change is what a caller sees for every id that is not an
#     escape. An unknown id is still `resource not found`; an id the
#     filesystem itself will not accept -- a NUL byte, six hundred characters
#     -- is still reported with the operation and the absolute path it named,
#     because that is what the open rendered before the root existed and it
#     is what the API answers with. Two things carry that: `rootErr`, which
#     restates a rooted error with the operation and absolute path the
#     unrooted one used, and `notFound`, which maps "not there" to the
#     driver's own not-found error and leaves every other answer alone. The
#     second is why the existence check is not replaced by a rooted existence
#     check: the old check could not tell a missing document from a name the
#     filesystem rejects, and the operation itself can, so the check goes
#     rather than moving.
#
#   * The sweeper. `api/utils/utils_store.go` builds a store with a lifetime
#     by starting a goroutine on `time.Tick`. `time.Tick` hands back a
#     channel and keeps no handle on the ticker behind it, so there is
#     nothing to stop and nobody holds anything that could stop it: the
#     goroutine waits for the life of the process, and every store ever built
#     with a lifetime leaves one behind. That is one defect. The second is
#     that it is the *only* thing that expires anything, so between two of
#     its ticks `IsSet` and `Get` say an entry is gone while `Keys` and `Map`
#     still list it -- four views of one store, two of which consult the
#     deadline and two of which read the backing map.
#
#     Both go the same way. `live` is the one place the deadline is
#     consulted, it deletes what has run out, and all four views ask through
#     it; with nothing left to sweep, the goroutine and the ticker go with
#     the sweep.
#
#     `Map` had a third defect that the rewrite settles on the way past. It
#     took the read lock and then called `Keys` and `Get`, which take it
#     again; `sync.RWMutex` will not grant a second read lock while a writer
#     is waiting, so a `Set` arriving between the outer and the inner
#     acquisition stopped all three permanently. The views now take the write
#     lock -- `live` can delete, so they have to -- and reach the map
#     directly instead of through each other.
#
#   * The repository's own test for it is written in simulated time.
#     `synctest.Test` gives its function a clock that jumps whenever
#     everything inside the bubble is waiting, so an entry's whole lifetime
#     passes at no cost, and the bubble refuses to finish while anything
#     inside it is still waiting on that clock -- which is the same statement
#     as "nothing is left running behind the store", made by the test
#     framework rather than by an assertion.
#
# Every edit below is anchored on text that exists in the tree step 6 leaves
# behind. `edit` requires its anchor to occur exactly once and `sub` requires
# exactly the stated number of occurrences, so a file that has moved
# underneath this script fails loudly instead of being written back
# unchanged.
set -euo pipefail

cd /app
export GO111MODULE=on GOFLAGS=-mod=mod

# ---------------------------------------------------------------------------
# 1. The volume half of the vfs driver's document access. Written out whole
#    rather than assembled from anchors, because every function in it changes
#    and the three helpers the snapshot half shares -- `notFound`, `rootErr`
#    and the reader -- live here.
#
#    What is unchanged: the names and signatures of everything the rest of
#    the package calls, the log line `getVolPath` writes, the id format, and
#    the fact that a document is one JSON object per file.
# ---------------------------------------------------------------------------

cat > drivers/storage/vfs/storage/vfs_storage_vol.go <<'GO'
package storage

import (
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"os"
	"sync/atomic"

	"github.com/codedellemc/libstorage/api/types"
	"github.com/codedellemc/libstorage/api/utils"
)

// jsonGlobPatt matches every document in a data directory. The pattern
// is relative because the directory is named by the root it is listed
// through, not by a prefix on the pattern.
const jsonGlobPatt = "*.json"

// volName is the name of a volume's document inside the volumes
// directory. It is a name, not a path: the driver hands it to the root
// it holds on that directory, which refuses a name that leaves.
func volName(volumeID string) string {
	return fmt.Sprintf("%s.json", volumeID)
}

func (d *driver) getVolPath(volumeID string) string {
	p := fmt.Sprintf("%s/%s.json", d.volPath, volumeID)
	d.ctx.WithField("vol.path", p).Info(p)
	return p
}

func (d *driver) getVolumeByID(volumeID string) (*types.Volume, error) {
	volJSONPath := d.getVolPath(volumeID)

	v, err := readJSON[types.Volume](d.volRoot, volName(volumeID), volJSONPath)
	if err != nil {
		return nil, notFound(err, volumeID)
	}

	return v, nil
}

// notFound gives the driver's answer for a document that is not there,
// and leaves every other answer alone. The existence check that used to
// stand in front of each of these operations asked the filesystem the
// same question one call earlier, and it could not tell a document that
// is not there from a name the filesystem would not accept, so the
// second answer is the one the operation itself gives.
func notFound(err error, id string) error {
	if errors.Is(err, fs.ErrNotExist) {
		return utils.NewNotFoundError(id)
	}
	return err
}

// rootErr restates an error from a rooted operation in the terms the
// unrooted operation used: the name of the operation a caller has been
// reading since before the root existed, and the absolute path of the
// document it was asked for.
func rootErr(err error, absPath string) error {
	var pathErr *fs.PathError
	if errors.As(err, &pathErr) {
		return &fs.PathError{Op: "open", Path: absPath, Err: pathErr.Err}
	}
	return err
}

// readJSON decodes the JSON document called name, inside root, into a
// freshly allocated T. The volume reader and the snapshot reader were
// this function written twice, the resource type being the whole of the
// difference.
func readJSON[T any](root *os.Root, name, absPath string) (*T, error) {
	f, err := root.Open(name)
	if err != nil {
		return nil, rootErr(err, absPath)
	}
	defer f.Close()

	v := new(T)
	if err := json.NewDecoder(f).Decode(v); err != nil {
		return nil, err
	}

	return v, nil
}

func (d *driver) writeVolume(v *types.Volume) error {
	volJSONPath := d.getVolPath(v.ID)

	f, err := d.volRoot.Create(volName(v.ID))
	if err != nil {
		return rootErr(err, volJSONPath)
	}
	defer f.Close()

	enc := json.NewEncoder(f)

	if err := enc.Encode(v); err != nil {
		return err
	}

	return nil
}

func (d *driver) getVolJSONs() ([]string, error) {
	return fs.Glob(d.volRoot.FS(), jsonGlobPatt)
}

func (d *driver) newVolumeID() string {
	return fmt.Sprintf("vfs-%03d", atomic.AddInt64(&d.volCount, 1))
}
GO

# ---------------------------------------------------------------------------
# 2. The snapshot half, the same shape against the other root. It keeps no
#    helpers of its own; `gotil` leaves its import list with the check.
# ---------------------------------------------------------------------------

cat > drivers/storage/vfs/storage/vfs_storage_snap.go <<'GO'
package storage

import (
	"encoding/json"
	"fmt"
	"io/fs"
	"sync/atomic"

	"github.com/codedellemc/libstorage/api/types"
)

// snapName is the name of a snapshot's document inside the snapshots
// directory, on the same terms as volName.
func snapName(snapshotID string) string {
	return fmt.Sprintf("%s.json", snapshotID)
}

func (d *driver) getSnapPath(snapshotID string) string {
	return fmt.Sprintf("%s/%s.json", d.snapPath, snapshotID)
}

func (d *driver) getSnapshotByID(snapshotID string) (*types.Snapshot, error) {
	snapJSONPath := d.getSnapPath(snapshotID)

	s, err := readJSON[types.Snapshot](
		d.snapRoot, snapName(snapshotID), snapJSONPath)
	if err != nil {
		return nil, notFound(err, snapshotID)
	}

	return s, nil
}

func (d *driver) writeSnapshot(s *types.Snapshot) error {
	snapJSONPath := d.getSnapPath(s.ID)

	f, err := d.snapRoot.Create(snapName(s.ID))
	if err != nil {
		return rootErr(err, snapJSONPath)
	}
	defer f.Close()

	enc := json.NewEncoder(f)

	if err := enc.Encode(s); err != nil {
		return err
	}

	return nil
}

func (d *driver) getSnapJSONs() ([]string, error) {
	return fs.Glob(d.snapRoot.FS(), jsonGlobPatt)
}

func (d *driver) newSnapshotID(volumeID string) string {
	return fmt.Sprintf(
		"%s-%03d", volumeID, atomic.AddInt64(&d.snapCount, 1))
}
GO

# ---------------------------------------------------------------------------
# 3. The store's own test, in simulated time. It goes in `package utils`
#    beside the tests that are already there, and uses the same assertion
#    library they do.
# ---------------------------------------------------------------------------

cat > api/utils/utils_store_ttl_test.go <<'GO'
package utils

import (
	"fmt"
	"testing"
	"testing/synctest"
	"time"

	"github.com/stretchr/testify/assert"
)

// TestTTLStoreExpiresAnEntryEverywhereAtOnce asks a store with a
// lifetime what it knows about an entry once that lifetime has run out,
// and asks it from all four of the views a caller has.
//
// The lifetime passes in simulated time. `synctest.Test` gives the
// function it runs a clock of its own that jumps forward whenever
// everything inside the bubble is waiting, so a sleep of two lifetimes
// costs the suite nothing -- and the bubble does not finish while
// anything inside it is still waiting on that clock, which is the other
// half of what this test is for.
func TestTTLStoreExpiresAnEntryEverywhereAtOnce(t *testing.T) {
	synctest.Test(t, func(t *testing.T) {
		s := NewTTLStore(time.Minute, true)
		s.Set("myVal", "hello")

		assert.True(t, s.IsSet("myVal"))
		assert.Equal(t, "hello", s.Get("myVal"))
		assert.Equal(t, []string{"myval"}, s.Keys())

		time.Sleep(2 * time.Minute)

		assert.False(t, s.IsSet("myVal"))
		assert.Nil(t, s.Get("myVal"))
		assert.Empty(t, s.Keys())
		assert.Empty(t, s.Map())
	})
}

// TestTTLStoreMapIsNotBlockedByAWriter reads the store as a map while
// another goroutine writes to it. The two used to be able to meet in a
// way neither of them came back from: the map view took the read lock
// and then called two exported readers that took it again, and a write
// arriving between the outer acquisition and an inner one stopped all
// three.
//
// The store is filled before the writer starts so that one view of it
// takes a thousand inner acquisitions rather than one. With a single
// entry that window is a handful of instructions wide and whether the
// old code stopped was the scheduler's decision rather than the code's.
func TestTTLStoreMapIsNotBlockedByAWriter(t *testing.T) {
	s := NewStore()

	const entries = 1000
	for i := 0; i < entries; i++ {
		s.Set(fmt.Sprintf("k%04d", i), i)
	}

	stop := make(chan struct{})
	defer close(stop)
	go func() {
		for i := 0; ; i++ {
			select {
			case <-stop:
				return
			default:
			}
			s.Set("w", i)
		}
	}()

	for i := 0; i < 200; i++ {
		assert.True(t, len(s.Map()) >= entries)
	}
}
GO

# ---------------------------------------------------------------------------
# 4. The rung and the two files that are edited rather than rewritten.
# ---------------------------------------------------------------------------

python3 - <<'PY'
import io
import sys


def read(path):
    return io.open(path).read()


def write(path, text):
    io.open(path, "w").write(text)


def edit(path, *pairs):
    """Apply (old, new) replacements to a file; every `old` must occur once."""
    text = read(path)
    for old, new in pairs:
        n = text.count(old)
        if n != 1:
            sys.exit("%s: anchor occurs %d times, expected 1:\n%s" %
                     (path, n, old))
        text = text.replace(old, new)
    write(path, text)


def sub(path, old, new, count):
    """Replace every `old` in a file; there must be exactly `count` of them."""
    text = read(path)
    n = text.count(old)
    if n != count:
        sys.exit("%s: %r occurs %d times, expected %d" % (path, old, n, count))
    write(path, text.replace(old, new))

# -------------------------------------------------------------------------
# 5. The rung. 1.25 is where `os.Root` grew `MkdirAll`, `RemoveAll` and
#    `FS`, and where `testing/synctest` stopped being an experiment. It is
#    a statement about the code, not a note about the toolchain the code
#    is built with.
# -------------------------------------------------------------------------

edit("go.mod", ("\ngo 1.23\n", "\ngo 1.25\n"))

# -------------------------------------------------------------------------
# 6. The driver. Two roots on the struct, opened in `Init`; the glob
#    patterns go, because a pattern handed to a root is relative to it;
#    the three `os.MkdirAll` calls that build a volume's own directory
#    become one call on the volumes root, which also retires the last
#    `path.Join` in the file; and the two removals stop asking whether the
#    cleaned path is there and start asking the root to remove a name.
# -------------------------------------------------------------------------

edit("drivers/storage/vfs/storage/vfs_storage.go",
     ('\t"errors"\n'
      '\t"fmt"\n'
      '\t"os"\n'
      '\t"path"\n'
      '\t"time"\n',
      '\t"errors"\n'
      '\t"fmt"\n'
      '\t"os"\n'
      '\t"time"\n'),

     ('\t"github.com/akutz/gotil"\n'
      '\t"github.com/codedellemc/libstorage/api/context"\n',
      '\t"github.com/codedellemc/libstorage/api/context"\n'),

     ('\tvolJSONGlobPatt  string\n'
      '\tsnapJSONGlobPatt string\n'
      '\tvolCount         int64\n'
      '\tsnapCount        int64\n'
      '\n'
      '\tvolPath  string\n'
      '\tsnapPath string\n'
      '}',
      '\tvolCount  int64\n'
      '\tsnapCount int64\n'
      '\n'
      '\tvolPath  string\n'
      '\tsnapPath string\n'
      '\n'
      '\t// One root per data directory. The volumes root cannot reach the\n'
      '\t// snapshots directory and neither can reach anything above them, so\n'
      '\t// a caller-supplied id that walks upwards is refused where the name\n'
      '\t// is resolved rather than cleaned into a path one directory over.\n'
      '\tvolRoot  *os.Root\n'
      '\tsnapRoot *os.Root\n'
      '}'),

     ('\td.volJSONGlobPatt = fmt.Sprintf("%s/*.json", d.volPath)\n'
      '\td.snapJSONGlobPatt = fmt.Sprintf("%s/*.json", d.snapPath)\n'
      '\n'
      '\tvolJSONPaths, err := d.getVolJSONs()\n'
      '\tif err != nil {\n'
      '\t\treturn nil\n'
      '\t}\n'
      '\td.volCount = int64(len(volJSONPaths)) - 1\n'
      '\n'
      '\tsnapJSONPaths, err := d.getSnapJSONs()\n'
      '\tif err != nil {\n'
      '\t\treturn nil\n'
      '\t}\n'
      '\td.snapCount = int64(len(snapJSONPaths)) - 1\n',
      '\tvolRoot, err := os.OpenRoot(d.volPath)\n'
      '\tif err != nil {\n'
      '\t\treturn err\n'
      '\t}\n'
      '\td.volRoot = volRoot\n'
      '\n'
      '\tsnapRoot, err := os.OpenRoot(d.snapPath)\n'
      '\tif err != nil {\n'
      '\t\treturn err\n'
      '\t}\n'
      '\td.snapRoot = snapRoot\n'
      '\n'
      '\tvolJSONNames, err := d.getVolJSONs()\n'
      '\tif err != nil {\n'
      '\t\treturn nil\n'
      '\t}\n'
      '\td.volCount = int64(len(volJSONNames)) - 1\n'
      '\n'
      '\tsnapJSONNames, err := d.getSnapJSONs()\n'
      '\tif err != nil {\n'
      '\t\treturn nil\n'
      '\t}\n'
      '\td.snapCount = int64(len(snapJSONNames)) - 1\n'),

     ('\tvolJSONPaths, err := d.getVolJSONs()\n'
      '\tif err != nil {\n'
      '\t\treturn nil, err\n'
      '\t}\n'
      '\n'
      '\tvolumes := []*types.Volume{}\n'
      '\n'
      '\tfor _, volJSONPath := range volJSONPaths {\n'
      '\t\tv, err := readJSON[types.Volume](volJSONPath)\n',
      '\tvolJSONNames, err := d.getVolJSONs()\n'
      '\tif err != nil {\n'
      '\t\treturn nil, err\n'
      '\t}\n'
      '\n'
      '\tvolumes := []*types.Volume{}\n'
      '\n'
      '\tfor _, name := range volJSONNames {\n'
      '\t\tv, err := readJSON[types.Volume](d.volRoot, name,\n'
      '\t\t\tfmt.Sprintf("%s/%s", d.volPath, name))\n'),

     ('\tsnapJSONPaths, err := d.getSnapJSONs()\n'
      '\tif err != nil {\n'
      '\t\treturn nil, err\n'
      '\t}\n'
      '\n'
      '\tsnapshots := []*types.Snapshot{}\n'
      '\n'
      '\tfor _, snapJSONPath := range snapJSONPaths {\n'
      '\t\ts, err := readJSON[types.Snapshot](snapJSONPath)\n',
      '\tsnapJSONNames, err := d.getSnapJSONs()\n'
      '\tif err != nil {\n'
      '\t\treturn nil, err\n'
      '\t}\n'
      '\n'
      '\tsnapshots := []*types.Snapshot{}\n'
      '\n'
      '\tfor _, name := range snapJSONNames {\n'
      '\t\ts, err := readJSON[types.Snapshot](d.snapRoot, name,\n'
      '\t\t\tfmt.Sprintf("%s/%s", d.snapPath, name))\n'),

     ('\tvolDir := path.Join(vfs.VolumesDirPath(d.config), newVol.ID)\n'
      '\tos.MkdirAll(volDir, 0755)\n',
      '\td.volRoot.MkdirAll(newVol.ID, 0755)\n'),

     ('\tvolJSONPath := d.getVolPath(volumeID)\n'
      '\tif !gotil.FileExists(volJSONPath) {\n'
      '\t\treturn utils.NewNotFoundError(volumeID)\n'
      '\t}\n'
      '\tos.Remove(volJSONPath)\n'
      '\n'
      '\tvolDir := path.Join(vfs.VolumesDirPath(d.config), volumeID)\n'
      '\tos.RemoveAll(volDir)\n',
      '\tvolJSONPath := d.getVolPath(volumeID)\n'
      '\tif err := d.volRoot.Remove(volName(volumeID)); err != nil {\n'
      '\t\treturn notFound(rootErr(err, volJSONPath), volumeID)\n'
      '\t}\n'
      '\n'
      '\td.volRoot.RemoveAll(volumeID)\n'),

     ('\tsnapJSONPath := d.getSnapPath(snapshotID)\n'
      '\tif !gotil.FileExists(snapJSONPath) {\n'
      '\t\treturn utils.NewNotFoundError(snapshotID)\n'
      '\t}\n'
      '\tos.Remove(snapJSONPath)\n',
      '\tsnapJSONPath := d.getSnapPath(snapshotID)\n'
      '\tif err := d.snapRoot.Remove(snapName(snapshotID)); err != nil {\n'
      '\t\treturn notFound(rootErr(err, snapJSONPath), snapshotID)\n'
      '\t}\n'))

# The two remaining volume-directory creations are the same two lines and
# differ only in which function they stand in, so they go together.
sub("drivers/storage/vfs/storage/vfs_storage.go",
    ('\tvolDir := path.Join(vfs.VolumesDirPath(d.config), v.ID)\n'
     '\tos.MkdirAll(volDir, 0755)\n'),
    '\td.volRoot.MkdirAll(v.ID, 0755)\n', 2)

# -------------------------------------------------------------------------
# 7. The store. The sweeper goes, `live` takes over the deadline for all
#    four views, and the views take the write lock because `live` deletes.
# -------------------------------------------------------------------------

edit("api/utils/utils_store.go",
     ('\t\tstore.startCleanupTimer()\n'
      '\t}\n'
      '\treturn store\n'
      '}\n'
      '\n'
      'func (s *keyValueStore) cleanup() {\n'
      '\ts.Lock()\n'
      '\tdefer s.Unlock()\n'
      '\tfor k, v := range s.timers {\n'
      '\t\tif v.expired() {\n'
      '\t\t\tdelete(s.store, k)\n'
      '\t\t\tdelete(s.timers, k)\n'
      '\t\t\tslog.Debug("expiring cached key", "key", k)\n'
      '\t\t}\n'
      '\t}\n'
      '}\n'
      '\n'
      'func (s *keyValueStore) startCleanupTimer() {\n'
      '\tduration := s.ttl\n'
      '\tif duration < time.Second {\n'
      '\t\tduration = time.Second\n'
      '\t}\n'
      '\tticker := time.Tick(duration)\n'
      '\tgo (func() {\n'
      '\t\tfor {\n'
      '\t\t\tselect {\n'
      '\t\t\tcase <-ticker:\n'
      '\t\t\t\ts.cleanup()\n'
      '\t\t\t}\n'
      '\t\t}\n'
      '\t})()\n'
      '}\n',
      '\t}\n'
      '\treturn store\n'
      '}\n'
      '\n'
      '// live reports whether k names an entry that is there and has not run\n'
      '// out, and forgets the entry if it has. Every view of the store asks\n'
      '// through here, so the four of them give one answer rather than four,\n'
      '// and an entry stops existing when the store is next asked about it\n'
      '// rather than when something else gets round to sweeping it up.\n'
      '//\n'
      '// The caller holds the lock, and it is the write lock, because the\n'
      '// answer to the question can be a deletion.\n'
      'func (s *keyValueStore) live(k string) bool {\n'
      '\t_, vok := s.store[k]\n'
      '\tif s.timers == nil || !vok {\n'
      '\t\treturn vok\n'
      '\t}\n'
      '\tttlt, tok := s.timers[k]\n'
      '\tif !tok || ttlt.expired() {\n'
      '\t\tdelete(s.store, k)\n'
      '\t\tdelete(s.timers, k)\n'
      '\t\tslog.Debug("expiring cached key", "key", k)\n'
      '\t\treturn false\n'
      '\t}\n'
      '\tif !s.hl {\n'
      '\t\tttlt.touch(s.ttl)\n'
      '\t}\n'
      '\treturn true\n'
      '}\n'
      '\n'
      '// liveKeys is the store\'s keys with the entries that have run out left\n'
      '// out of it -- and gone from the store. The caller holds the lock.\n'
      'func (s *keyValueStore) liveKeys() []string {\n'
      '\tkeys := slices.AppendSeq(\n'
      '\t\tmake([]string, 0, len(s.store)), maps.Keys(s.store))\n'
      '\treturn slices.DeleteFunc(keys, '
      'func(k string) bool { return !s.live(k) })\n'
      '}\n'),

     ('func (s *keyValueStore) Map() map[string]interface{} {\n'
      '\ts.RLock()\n'
      '\tdefer s.RUnlock()\n'
      '\tm := map[string]interface{}{}\n'
      '\tfor _, k := range s.Keys() {\n'
      '\t\tm[k] = s.Get(k)\n'
      '\t}\n'
      '\treturn m\n'
      '}\n'
      '\n'
      'func (s *keyValueStore) IsSet(k string) bool {\n'
      '\ts.RLock()\n'
      '\tdefer s.RUnlock()\n'
      '\tk = strings.ToLower(k)\n'
      '\t_, vok := s.store[k]\n'
      '\tif s.timers == nil || !vok {\n'
      '\t\treturn vok\n'
      '\t}\n'
      '\tttlt, tok := s.timers[k]\n'
      '\tif !tok || ttlt.expired() {\n'
      '\t\treturn false\n'
      '\t}\n'
      '\tif !s.hl {\n'
      '\t\tttlt.touch(s.ttl)\n'
      '\t}\n'
      '\treturn true\n'
      '}\n'
      '\n'
      'func (s *keyValueStore) Get(k string) interface{} {\n'
      '\ts.RLock()\n'
      '\tdefer s.RUnlock()\n'
      '\tk = strings.ToLower(k)\n'
      '\tv, vok := s.store[k]\n'
      '\tif s.timers == nil || !vok {\n'
      '\t\treturn v\n'
      '\t}\n'
      '\tttlt, tok := s.timers[k]\n'
      '\tif !tok || ttlt.expired() {\n'
      '\t\treturn nil\n'
      '\t}\n'
      '\tif !s.hl {\n'
      '\t\tttlt.touch(s.ttl)\n'
      '\t}\n'
      '\treturn v\n'
      '}\n',
      'func (s *keyValueStore) Map() map[string]interface{} {\n'
      '\ts.Lock()\n'
      '\tdefer s.Unlock()\n'
      '\tm := map[string]interface{}{}\n'
      '\tfor _, k := range s.liveKeys() {\n'
      '\t\tm[k] = s.store[k]\n'
      '\t}\n'
      '\treturn m\n'
      '}\n'
      '\n'
      'func (s *keyValueStore) IsSet(k string) bool {\n'
      '\ts.Lock()\n'
      '\tdefer s.Unlock()\n'
      '\treturn s.live(strings.ToLower(k))\n'
      '}\n'
      '\n'
      'func (s *keyValueStore) Get(k string) interface{} {\n'
      '\ts.Lock()\n'
      '\tdefer s.Unlock()\n'
      '\tk = strings.ToLower(k)\n'
      '\tif !s.live(k) {\n'
      '\t\treturn nil\n'
      '\t}\n'
      '\treturn s.store[k]\n'
      '}\n'),

     ('func (s *keyValueStore) Keys() []string {\n'
      '\ts.RLock()\n'
      '\tdefer s.RUnlock()\n'
      '\treturn slices.AppendSeq(\n'
      '\t\tmake([]string, 0, len(s.store)), maps.Keys(s.store))\n'
      '}\n',
      'func (s *keyValueStore) Keys() []string {\n'
      '\ts.Lock()\n'
      '\tdefer s.Unlock()\n'
      '\treturn s.liveKeys()\n'
      '}\n'))

PY

# ---------------------------------------------------------------------------
# 8. Nothing joined or left the module graph here: every import added above
#    is from the standard library. Then the checks step 1 established, plus
#    the cross-compile step 3 added.
# ---------------------------------------------------------------------------

go mod tidy

unformatted=$(gofmt -l -s $(find . -name '*.go' -not -path './vendor/*'))
if [ -n "$unformatted" ]; then
    echo "not gofmt -s clean:" >&2
    echo "$unformatted" >&2
    exit 1
fi

go build ./...
go vet ./...
GOOS=darwin go build ./...
