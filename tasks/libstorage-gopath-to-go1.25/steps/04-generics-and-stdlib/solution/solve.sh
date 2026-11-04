#!/usr/bin/env bash
# Reference solution, step 4: the schema stops being generated, the bodies that
# were written out once per type become one generic declaration each, sorting
# and map copying move to the standard library's own helpers, and `io/ioutil`
# is retired.
#
# The shape of the change
# -----------------------
# Four things in this repository exist because the language could not say what
# the author meant, and one exists because the standard library has moved.
#
#   * `api/utils/schema/schema_generated.go` is `libstorage.json` pasted into a
#     raw string literal by a Makefile rule. Nothing reruns that rule; the two
#     copies agree because somebody ran `make` once, years ago. `//go:embed`
#     is the language saying it instead, so the document moves next to the
#     package that reads it and the generated file and its rule both go.
#
#   * Six of `keyValueStore`'s getters have the same four-line body with a
#     different type written into it. They cannot become one method -- Go has
#     no generic methods, and `types.Store` names all six -- but they can
#     become one generic function with six one-line callers, which is what the
#     difference between them was always worth.
#
#   * `readVolume` and `readSnapshot` are the same function twice. Here the
#     type is not in an interface, so the two callers can name it and the two
#     declarations collapse to one outright.
#
#   * Three `sort.Interface` adapters exist to tell `sort.Sort` how to compare
#     two strings and two volume IDs. `slices.Sort` already knows the first and
#     `slices.SortFunc` with `cmp.Compare` says the second in one line.
#
#   * `io/ioutil` has been deprecated since Go 1.16 and every one of its ten
#     uses here has a named replacement in `os` or `io`. It cannot be left
#     alone once the module's language version rises: staticcheck reads a
#     deprecation against the version the module claims, so `go 1.21` without
#     this section is ten SA1019 findings that were not there at `go 1.17`.
#
# The five map-copy loops that become `maps.Copy` are the same kind of change
# in miniature and are done in the same pass.
#
# Every edit below is anchored on text that exists in the tree step 3 leaves
# behind. `edit` requires its anchor to occur exactly once and `sub` requires
# exactly the stated number of occurrences, so a file that has moved underneath
# this script fails loudly instead of being written back unchanged.
set -euo pipefail

cd /app
export GO111MODULE=on GOFLAGS=-mod=mod

# ---------------------------------------------------------------------------
# 1. The schema document moves into the package that reads it, and the file
#    generated from it goes. `//go:embed` can only reach files in its own
#    package directory, which is why the document moves rather than being
#    pointed at where it lay; nothing but the retired Makefile rule referred
#    to the old path.
# ---------------------------------------------------------------------------
mv libstorage.json api/utils/schema/libstorage.json
rm api/utils/schema/schema_generated.go

# ---------------------------------------------------------------------------
# 2. With nothing left to generate, this package's Makefile is what every
#    other framework package's Makefile already is: the shared test rules and
#    nothing else. Compare api/utils/filters/Makefile, which is this line.
#    The file has to stay -- test.mk recurses into ./api/utils/schema for
#    build-tests, test and clean.
# ---------------------------------------------------------------------------
cat > api/utils/schema/Makefile <<'MK'
include ../../../test-framework-pkg.mk
MK

# ---------------------------------------------------------------------------
# 3. Sorting. The two exported adapters went with the two functions that used
#    them; nothing outside this file ever named them, and an adapter whose
#    only job is to carry a comparison to sort.Sort has no job once the
#    comparison can be passed directly. SortVolumeByID still returns its
#    argument, because drivers/storage/vfs/storage/vfs_storage.go uses the
#    return value.
# ---------------------------------------------------------------------------
cat > api/utils/utils_sort.go <<'GO'
package utils

import (
	"cmp"
	"slices"

	"github.com/codedellemc/libstorage/api/types"
)

// SortVolumeByID sorts the volumes by their IDs.
func SortVolumeByID(volumes []*types.Volume) []*types.Volume {
	slices.SortFunc(volumes, func(a, b *types.Volume) int {
		return cmp.Compare(a.ID, b.ID)
	})
	return volumes
}

// SortByString sorts the strings.
func SortByString(strings []string) []string {
	slices.Sort(strings)
	return strings
}
GO

# ---------------------------------------------------------------------------
# 4. Everything else, as anchored edits.
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


def sweep(path, imports, *counts):
    """Fix a file's imports, then replace each (old, new, n) call site."""
    edit(path, *imports)
    for old, new, n in counts:
        sub(path, old, new, n)


# -------------------------------------------------------------------------
# 5. The module's language version. Generics need 1.18; `slices`, `maps` and
#    `cmp` need 1.21. Nothing below compiles at 1.17, and the version in
#    go.mod is what the compiler and the linters both read, so it is a
#    statement about the code rather than a note about the toolchain.
# -------------------------------------------------------------------------
edit("go.mod", ("\ngo 1.17\n", "\ngo 1.21\n"))

# -------------------------------------------------------------------------
# 6. The schema. The build no longer has a generated source to make, so it
#    no longer has a rule that makes one; api_version_generated.go is still
#    generated and still listed, because that one carries the git describe
#    output and cannot be a constant in the tree.
# -------------------------------------------------------------------------
edit("Makefile",
     ("GENERATED_SRCS := ./api/api_version_generated.go \\\n"
      "  ./api/utils/schema/schema_generated.go\n",
      "GENERATED_SRCS := ./api/api_version_generated.go\n"))

edit("api/utils/schema/schema.go",
     ('import (\n'
      '\t"bytes"\n'
      '\t"encoding/json"\n',
      'import (\n'
      '\t"bytes"\n'
      '\t_ "embed"\n'
      '\t"encoding/json"\n'),

     ('var (\n'
      '\tjsonSchema = []byte(JSONSchema)\n'
      '\n',
      '// jsonSchema is the API schema document, read at build time from the\n'
      '// file itself. It used to be read from a Go source file holding the\n'
      '// same bytes in a raw string literal, which a Makefile rule produced\n'
      '// and nobody reproduced.\n'
      '//\n'
      '//go:embed libstorage.json\n'
      'var jsonSchema []byte\n'
      '\n'
      'var (\n'))

# -------------------------------------------------------------------------
# 7. The store getters. `stored` is a function rather than a method because
#    a method cannot introduce a type parameter of its own, and the six
#    names have to survive: types.Store demands all six.
#
#    The coercing getters are deliberately untouched. GetString renders a
#    stored int as text, GetBool parses a stored string, the pointer forms
#    answer nil when a parse fails and a pointer to a copy when it does not
#    -- none of that is "the value if it is a T", and sweeping them into
#    `stored` would compile and would silently delete all of it.
# -------------------------------------------------------------------------
edit("api/utils/utils_store.go",
     ('func (s *keyValueStore) GetStore(k string) types.Store {\n'
      '\tv := s.Get(k)\n'
      '\tswitch tv := v.(type) {\n'
      '\tcase types.Store:\n'
      '\t\treturn tv\n'
      '\tdefault:\n'
      '\t\treturn nil\n'
      '\t}\n'
      '}\n',

      '// stored returns the value under k when it is a T, and the zero value\n'
      '// of T otherwise. Six of the getters below were this function with the\n'
      '// type written out; the type was the only thing they disagreed about,\n'
      '// which is what makes it a type parameter rather than six bodies.\n'
      'func stored[T any](s *keyValueStore, k string) T {\n'
      '\tv, _ := s.Get(k).(T)\n'
      '\treturn v\n'
      '}\n'
      '\n'
      'func (s *keyValueStore) GetStore(k string) types.Store {\n'
      '\treturn stored[types.Store](s, k)\n'
      '}\n'),

     ('func (s *keyValueStore) GetStringSlice(k string) []string {\n'
      '\tv := s.Get(k)\n'
      '\tswitch tv := v.(type) {\n'
      '\tcase []string:\n'
      '\t\treturn tv\n'
      '\tdefault:\n'
      '\t\treturn nil\n'
      '\t}\n'
      '}\n',
      'func (s *keyValueStore) GetStringSlice(k string) []string {\n'
      '\treturn stored[[]string](s, k)\n'
      '}\n'),

     ('func (s *keyValueStore) GetIntSlice(k string) []int {\n'
      '\tv := s.Get(k)\n'
      '\tswitch tv := v.(type) {\n'
      '\tcase []int:\n'
      '\t\treturn tv\n'
      '\tdefault:\n'
      '\t\treturn nil\n'
      '\t}\n'
      '}\n',
      'func (s *keyValueStore) GetIntSlice(k string) []int {\n'
      '\treturn stored[[]int](s, k)\n'
      '}\n'),

     ('func (s *keyValueStore) GetBoolSlice(k string) []bool {\n'
      '\tv := s.Get(k)\n'
      '\tswitch tv := v.(type) {\n'
      '\tcase []bool:\n'
      '\t\treturn tv\n'
      '\tdefault:\n'
      '\t\treturn nil\n'
      '\t}\n'
      '}\n',
      'func (s *keyValueStore) GetBoolSlice(k string) []bool {\n'
      '\treturn stored[[]bool](s, k)\n'
      '}\n'),

     ('func (s *keyValueStore) GetMap(k string) map[string]interface{} {\n'
      '\tv := s.Get(k)\n'
      '\tswitch tv := v.(type) {\n'
      '\tcase map[string]interface{}:\n'
      '\t\treturn tv\n'
      '\tdefault:\n'
      '\t\treturn nil\n'
      '\t}\n'
      '}\n',
      'func (s *keyValueStore) GetMap(k string) map[string]interface{} {\n'
      '\treturn stored[map[string]interface{}](s, k)\n'
      '}\n'),

     ('func (s *keyValueStore) GetInstanceID(k string) *types.InstanceID {\n'
      '\tv := s.Get(k)\n'
      '\tswitch tv := v.(type) {\n'
      '\tcase (*types.InstanceID):\n'
      '\t\treturn tv\n'
      '\tdefault:\n'
      '\t\treturn nil\n'
      '\t}\n'
      '}\n',
      'func (s *keyValueStore) GetInstanceID(k string) *types.InstanceID {\n'
      '\treturn stored[*types.InstanceID](s, k)\n'
      '}\n'))

# -------------------------------------------------------------------------
# 8. The two VFS readers. `new(T)` is what `&types.Volume{}` was, and the
#    caller names the type it wanted, so nothing about the call sites is
#    less specific than it was.
# -------------------------------------------------------------------------
edit("drivers/storage/vfs/storage/vfs_storage_vol.go",
     ("\treturn readVolume(volJSONPath)\n",
      "\treturn readJSON[types.Volume](volJSONPath)\n"),

     ('func readVolume(path string) (*types.Volume, error) {\n'
      '\tf, err := os.Open(path)\n'
      '\tif err != nil {\n'
      '\t\treturn nil, err\n'
      '\t}\n'
      '\tdefer f.Close()\n'
      '\n'
      '\tv := &types.Volume{}\n'
      '\tif err := json.NewDecoder(f).Decode(v); err != nil {\n'
      '\t\treturn nil, err\n'
      '\t}\n'
      '\n'
      '\treturn v, nil\n'
      '}\n',

      '// readJSON decodes the JSON document at path into a freshly allocated\n'
      '// T. The volume reader and the snapshot reader were this function\n'
      '// written twice, the resource type being the whole of the difference.\n'
      'func readJSON[T any](path string) (*T, error) {\n'
      '\tf, err := os.Open(path)\n'
      '\tif err != nil {\n'
      '\t\treturn nil, err\n'
      '\t}\n'
      '\tdefer f.Close()\n'
      '\n'
      '\tv := new(T)\n'
      '\tif err := json.NewDecoder(f).Decode(v); err != nil {\n'
      '\t\treturn nil, err\n'
      '\t}\n'
      '\n'
      '\treturn v, nil\n'
      '}\n'))

edit("drivers/storage/vfs/storage/vfs_storage_snap.go",
     ("\treturn readSnapshot(snapJSONPath)\n",
      "\treturn readJSON[types.Snapshot](snapJSONPath)\n"),

     ('func readSnapshot(path string) (*types.Snapshot, error) {\n'
      '\tf, err := os.Open(path)\n'
      '\tif err != nil {\n'
      '\t\treturn nil, err\n'
      '\t}\n'
      '\tdefer f.Close()\n'
      '\n'
      '\tv := &types.Snapshot{}\n'
      '\tif err := json.NewDecoder(f).Decode(v); err != nil {\n'
      '\t\treturn nil, err\n'
      '\t}\n'
      '\n'
      '\treturn v, nil\n'
      '}\n'
      '\n', ''))

edit("drivers/storage/vfs/storage/vfs_storage.go",
     ("\t\tv, err := readVolume(volJSONPath)\n",
      "\t\tv, err := readJSON[types.Volume](volJSONPath)\n"),
     ("\t\ts, err := readSnapshot(snapJSONPath)\n",
      "\t\ts, err := readJSON[types.Snapshot](snapJSONPath)\n"))

# -------------------------------------------------------------------------
# 9. The third sort adapter, the one api/types kept to itself.
# -------------------------------------------------------------------------
edit("api/types/types_localdevices.go",
     ('\t"regexp"\n\t"sort"\n', '\t"regexp"\n\t"slices"\n'),
     ("\tsort.Sort(byString(keys))\n", "\tslices.Sort(keys)\n"),
     ('\n// byString  implements sort.Interface for []string.\n'
      'type byString []string\n'
      '\n'
      'func (a byString) Len() int           { return len(a) }\n'
      'func (a byString) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }\n'
      'func (a byString) Less(i, j int) bool { return a[i] < a[j] }\n', ''))

# -------------------------------------------------------------------------
# 10. The five loops whose whole body is "put every entry of that map into
#     this one". handlers_logging.go has a second loop over rec.Header()
#     three lines further down that formats rather than copies, which is why
#     each anchor here carries its body.
# -------------------------------------------------------------------------
edit("api/server/httputils/httputils.go",
     ('\t"encoding/json"\n\t"net/http"\n',
      '\t"encoding/json"\n\t"maps"\n\t"net/http"\n'),
     ("\tfor k, v := range rec.Header() {\n\t\tw.Header()[k] = v\n\t}\n",
      "\tmaps.Copy(w.Header(), rec.Header())\n"))

edit("api/context/context_logger.go",
     ('\t"fmt"\n\t"os"\n', '\t"fmt"\n\t"maps"\n\t"os"\n'),
     ("\t\tfor k, v := range tv.ContextLoggerFields() {\n"
      "\t\t\tfields[k] = v\n\t\t}\n",
      "\t\tmaps.Copy(fields, tv.ContextLoggerFields())\n"),
     ("\tfor k, v := range fields {\n\t\tentry.Data[k] = v\n\t}\n",
      "\tmaps.Copy(entry.Data, fields)\n"))

edit("api/server/handlers/handlers_logging.go",
     ('\t"io"\n\t"net"\n', '\t"io"\n\t"maps"\n\t"net"\n'),
     ("\tfor k, v := range rec.Header() {\n\t\tw.Header()[k] = v\n\t}\n",
      "\tmaps.Copy(w.Header(), rec.Header())\n"))

# -------------------------------------------------------------------------
# 11. io/ioutil, in the six packages that still import it. Every name has a
#     one-for-one replacement with the same signature and the same
#     behaviour: ReadAll moved to io, and ReadFile, WriteFile, TempDir and
#     TempFile moved to os, the last two under the names MkdirTemp and
#     CreateTemp.
#
#     handlers_schema_validator.go picks up maps at the same time, so that
#     its import block is rewritten once rather than twice.
# -------------------------------------------------------------------------
sweep("api/server/handlers/handlers_schema_validator.go",
      [('\t"fmt"\n\t"io/ioutil"\n\t"net/http"\n',
        '\t"fmt"\n\t"io"\n\t"maps"\n\t"net/http"\n')],
      ("ioutil.ReadAll(", "io.ReadAll(", 1),
      ("\tfor k, v := range rec.Header() {\n\t\tw.Header()[k] = v\n\t}\n",
       "\tmaps.Copy(w.Header(), rec.Header())\n", 1))

sweep("api/client/client_http.go",
      [('\t"io"\n\t"io/ioutil"\n', '\t"io"\n')],
      ("ioutil.ReadAll(", "io.ReadAll(", 1))

sweep("api/tests/tests_runners.go",
      [('\t"fmt"\n\t"io/ioutil"\n\t"os"\n', '\t"fmt"\n\t"os"\n')],
      ("ioutil.WriteFile(", "os.WriteFile(", 1))

sweep("api/tests/tests_utils.go",
      [('\t"io"\n\t"io/ioutil"\n\t"os"\n', '\t"io"\n\t"os"\n')],
      ("ioutil.TempDir(", "os.MkdirTemp(", 1))

sweep("api/utils/utils.go",
      [('\t"fmt"\n\t"io/ioutil"\n\t"os"\n', '\t"fmt"\n\t"os"\n')],
      ("ioutil.TempFile(", "os.CreateTemp(", 1))

sweep("api/utils/utils_auth.go",
      [('import (\n\t"io/ioutil"\n', 'import (\n\t"os"\n')],
      ("ioutil.ReadFile(", "os.ReadFile(", 1))

sweep("api/utils/utils_paths_test.go",
      [('\t"fmt"\n\t"io/ioutil"\n\t"os"\n', '\t"fmt"\n\t"os"\n')],
      ("ioutil.TempDir(", "os.MkdirTemp(", 1))

sweep("api/utils/utils_tls.go",
      [('\t"encoding/hex"\n\t"io/ioutil"\n\t"os"\n',
        '\t"encoding/hex"\n\t"io"\n\t"os"\n')],
      ("ioutil.ReadAll(", "io.ReadAll(", 1))

sweep("drivers/storage/libstorage/libstorage_driver.go",
      [('\t"crypto/tls"\n\t"io/ioutil"\n\t"net"\n', '\t"crypto/tls"\n\t"net"\n'),
       ('\t"net/http"\n\t"strings"\n', '\t"net/http"\n\t"os"\n\t"strings"\n')],
      ("ioutil.ReadFile(", "os.ReadFile(", 1))

sweep("drivers/storage/vfs/executor/vfs_executor.go",
      [('\t"errors"\n\t"io/ioutil"\n\t"os"\n', '\t"errors"\n\t"os"\n')],
      ("ioutil.WriteFile(", "os.WriteFile(", 1))
PY

# ---------------------------------------------------------------------------
# 12. Nothing in the module graph changes -- every import added here is from
#     the standard library and every one removed was too -- so `tidy` is here
#     to prove that rather than to do anything. Then the checks step 1
#     established, plus the cross-compile step 3 added.
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
