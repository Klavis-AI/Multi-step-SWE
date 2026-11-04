// Package rooted asks what the vfs storage driver does with an identifier
// that names something outside its own data directory, and what it still does
// with every identifier that does not.
//
// The driver turns a caller-supplied volume or snapshot id into a filename by
// formatting it into a directory name. `path.Join` cleans `..` rather than
// rejecting it, and the only guard on the result is an existence check, so an
// id that walks upwards names a file the driver was never meant to reach --
// and reads it, and deletes it. Four of the tests below are that walk, taken
// through the driver's own interface with nothing but ids a caller controls.
//
// The other three are the surface that has to survive: a volume's ordinary
// lifecycle, the message an unknown id is reported with, and the message an id
// the filesystem itself will not accept is reported with. The last of those is
// the sharp one. The existence check is true for every error that is not
// "does not exist", so a name carrying a NUL byte or six hundred characters
// reaches the open and is reported with the error the open renders -- an
// operation name and an absolute path that a caller has been reading since
// before this migration started.
//
// Everything here goes through `types.StorageDriver` as the server obtains it,
// from the service registry, with the driver's data directory under the test's
// own temporary directory.
package rooted

import (
	"fmt"
	"os"
	"path"
	"strings"
	"testing"

	gofig "github.com/akutz/gofig/types"

	apictx "github.com/codedellemc/libstorage/api/context"
	"github.com/codedellemc/libstorage/api/registry"
	"github.com/codedellemc/libstorage/api/server/services"
	"github.com/codedellemc/libstorage/api/types"
	"github.com/codedellemc/libstorage/api/utils"
	apicnfg "github.com/codedellemc/libstorage/api/utils/config"
	"github.com/codedellemc/libstorage/drivers/storage/vfs"

	_ "github.com/codedellemc/libstorage/drivers/storage/vfs/storage"
	_ "github.com/codedellemc/libstorage/imports/config"
)

const configYML = `
libstorage:
  logging:
    level: panic
  server:
    services:
      vfs:
        driver: vfs
`

type fixture struct {
	ctx types.Context
	drv types.StorageDriver
	cfg gofig.Config
}

// newVFS brings up one vfs service the way the server does and hands back the
// driver behind it, rooted at a directory this test owns.
func newVFS(t *testing.T) *fixture {
	t.Helper()
	os.Setenv("LIBSTORAGE_LOGGING_LEVEL", "panic")

	ctx := apictx.Background()
	pc := utils.NewPathConfig(ctx, t.TempDir(), "", t.TempDir())
	ctx = apictx.WithValue(ctx, apictx.PathConfigKey, pc)
	registry.ProcessRegisteredConfigs(ctx)

	if err := os.WriteFile(
		path.Join(pc.Etc, "config.yml"), []byte(configYML), 0644); err != nil {
		t.Fatalf("writing the service configuration: %v", err)
	}
	cfg, err := apicnfg.NewConfig(ctx)
	if err != nil {
		t.Fatalf("reading the service configuration: %v", err)
	}

	ctx = apictx.WithValue(ctx, apictx.ServerKey, t.Name())
	if err := services.Init(ctx, cfg); err != nil {
		t.Fatalf("starting the vfs service: %v", err)
	}
	svc := services.GetStorageService(ctx, "vfs")
	if svc == nil {
		t.Fatal("the vfs service did not register")
	}
	ctx = apictx.WithStorageService(ctx, svc)
	if ctx, err = apictx.WithStorageSession(ctx); err != nil {
		t.Fatalf("opening a session on the vfs service: %v", err)
	}
	return &fixture{ctx: ctx, drv: svc.Driver(), cfg: cfg}
}

func (f *fixture) createVolume(t *testing.T, name string) *types.Volume {
	t.Helper()
	sz := int64(1)
	v, err := f.drv.VolumeCreate(f.ctx, name,
		&types.VolumeCreateOpts{Size: &sz, Opts: utils.NewStore()})
	if err != nil {
		t.Fatalf("creating volume %q: %v", name, err)
	}
	return v
}

func (f *fixture) snapshotVolume(
	t *testing.T, volumeID, name string) *types.Snapshot {

	t.Helper()
	s, err := f.drv.VolumeSnapshot(f.ctx, volumeID, name, utils.NewStore())
	if err != nil {
		t.Fatalf("snapshotting volume %q: %v", volumeID, err)
	}
	return s
}

func (f *fixture) inspectVolume(id string) (*types.Volume, error) {
	return f.drv.VolumeInspect(f.ctx, id,
		&types.VolumeInspectOpts{Opts: utils.NewStore()})
}

func (f *fixture) inspectSnapshot(id string) (*types.Snapshot, error) {
	return f.drv.SnapshotInspect(f.ctx, id, utils.NewStore())
}

func (f *fixture) removeVolume(id string) error {
	return f.drv.VolumeRemove(f.ctx, id,
		&types.VolumeRemoveOpts{Opts: utils.NewStore()})
}

// The walk upwards, read.

func TestAVolumeIdentifierThatLeavesTheDataDirectoryIsRefused(t *testing.T) {
	f := newVFS(t)
	v := f.createVolume(t, "alpha")
	s := f.snapshotVolume(t, v.ID, "alpha-snap")

	id := "../snap/" + s.ID
	got, err := f.inspectVolume(id)
	if err == nil {
		t.Errorf("inspecting volume %q succeeded; it names a document "+
			"outside the volumes directory", id)
	}
	if got != nil {
		t.Errorf("inspecting volume %q returned %+v; it names a document "+
			"outside the volumes directory", id, got)
	}
}

func TestASnapshotIdentifierThatLeavesTheDataDirectoryIsRefused(t *testing.T) {
	f := newVFS(t)
	v := f.createVolume(t, "alpha")

	id := "../vol/" + v.ID
	got, err := f.inspectSnapshot(id)
	if err == nil {
		t.Errorf("inspecting snapshot %q succeeded; it names a document "+
			"outside the snapshots directory", id)
	}
	if got != nil {
		t.Errorf("inspecting snapshot %q returned %+v; it names a document "+
			"outside the snapshots directory", id, got)
	}
}

// The walk upwards, written.

func TestRemovingAVolumeCannotReachTheSnapshotsDirectory(t *testing.T) {
	f := newVFS(t)
	v := f.createVolume(t, "alpha")
	s := f.snapshotVolume(t, v.ID, "alpha-snap")

	id := "../snap/" + s.ID
	if err := f.removeVolume(id); err == nil {
		t.Errorf("removing volume %q succeeded; it names a document "+
			"outside the volumes directory", id)
	}
	if got, err := f.inspectSnapshot(s.ID); err != nil || got == nil {
		t.Errorf("snapshot %s is gone after removing volume %q "+
			"(inspect returned %+v, %v)", s.ID, id, got, err)
	}
}

func TestRemovingAVolumeCannotReachOutsideTheRootDirectory(t *testing.T) {
	f := newVFS(t)
	f.createVolume(t, "alpha")

	outside := path.Join(path.Dir(vfs.RootDir(f.cfg)), "outside.json")
	if err := os.WriteFile(outside, []byte(`{"id":"outside"}`), 0644); err != nil {
		t.Fatalf("seeding a file beside the root directory: %v", err)
	}

	id := "../../outside"
	if err := f.removeVolume(id); err == nil {
		t.Errorf("removing volume %q succeeded; it names a file outside "+
			"the root directory", id)
	}
	if _, err := os.Stat(outside); err != nil {
		t.Errorf("the file beside the root directory is gone after "+
			"removing volume %q: %v", id, err)
	}
}

// The surface that has to survive.

func TestTheVolumeLifecycleIsUnchanged(t *testing.T) {
	f := newVFS(t)

	v := f.createVolume(t, "alpha")
	if v.Name != "alpha" {
		t.Errorf("created volume is named %q, want %q", v.Name, "alpha")
	}
	if v.ID == "" {
		t.Error("created volume has no id")
	}

	got, err := f.inspectVolume(v.ID)
	if err != nil {
		t.Fatalf("inspecting volume %s: %v", v.ID, err)
	}
	if got.ID != v.ID || got.Name != v.Name {
		t.Errorf("inspect returned %s/%s, want %s/%s",
			got.ID, got.Name, v.ID, v.Name)
	}

	vols, err := f.drv.Volumes(f.ctx,
		&types.VolumesOpts{Opts: utils.NewStore()})
	if err != nil {
		t.Fatalf("listing volumes: %v", err)
	}
	var seen bool
	for _, x := range vols {
		if x.ID == v.ID {
			seen = true
		}
	}
	if !seen {
		t.Errorf("volume %s is not in the list of %d volumes",
			v.ID, len(vols))
	}

	s := f.snapshotVolume(t, v.ID, "alpha-snap")
	if gotS, err := f.inspectSnapshot(s.ID); err != nil || gotS == nil {
		t.Errorf("inspecting snapshot %s returned %+v, %v", s.ID, gotS, err)
	}

	if err := f.removeVolume(v.ID); err != nil {
		t.Fatalf("removing volume %s: %v", v.ID, err)
	}
	if _, err := f.inspectVolume(v.ID); err == nil {
		t.Errorf("volume %s is still there after being removed", v.ID)
	}
}

func TestAnUnknownIdentifierIsStillReportedAsNotFound(t *testing.T) {
	f := newVFS(t)
	f.createVolume(t, "alpha")

	if _, err := f.inspectVolume("vfs-999"); err == nil {
		t.Error("inspecting an unknown volume succeeded")
	} else if err.Error() != "resource not found" {
		t.Errorf("inspecting an unknown volume failed with %q, want %q",
			err.Error(), "resource not found")
	}

	if _, err := f.inspectSnapshot("vfs-999-999"); err == nil {
		t.Error("inspecting an unknown snapshot succeeded")
	} else if err.Error() != "resource not found" {
		t.Errorf("inspecting an unknown snapshot failed with %q, want %q",
			err.Error(), "resource not found")
	}

	if err := f.removeVolume("vfs-999"); err == nil {
		t.Error("removing an unknown volume succeeded")
	} else if err.Error() != "resource not found" {
		t.Errorf("removing an unknown volume failed with %q, want %q",
			err.Error(), "resource not found")
	}
}

func TestAnIdentifierTheFilesystemRejectsStillNamesItsPath(t *testing.T) {
	f := newVFS(t)
	f.createVolume(t, "alpha")

	volDir := vfs.VolumesDirPath(f.cfg)
	for _, c := range []struct {
		what string
		id   string
		why  string
	}{
		{"a name carrying a NUL byte", "vfs\x00-000", "invalid argument"},
		{"a name of six hundred characters", strings.Repeat("z", 600),
			"file name too long"},
	} {
		want := fmt.Sprintf("open %s: %s",
			path.Join(volDir, c.id+".json"), c.why)
		_, err := f.inspectVolume(c.id)
		if err == nil {
			t.Errorf("inspecting a volume by %s succeeded", c.what)
			continue
		}
		if err.Error() != want {
			t.Errorf("inspecting a volume by %s failed with\n  %q\nwant\n  %q",
				c.what, err.Error(), want)
		}
	}
}
