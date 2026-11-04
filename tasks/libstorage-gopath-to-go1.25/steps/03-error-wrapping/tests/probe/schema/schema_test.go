// Package schema asks where the API's JSON schema comes from, and whether it
// still works.
//
// The repository carries the schema twice. `libstorage.json` is the document;
// `api/utils/schema/schema_generated.go` is the same 18 202 bytes pasted into a
// Go raw string literal by a Makefile rule, and nothing regenerates it. The two
// agree today because someone ran `make` once. That is what retiring codegen
// means here: one copy, and Go source that obtains it at build time instead of
// a second copy that has to be kept in step by hand.
//
// The tests below look for the property, not for a path. Nothing here requires
// the document to live anywhere in particular, or the package that reads it to
// be spelled any particular way. The two graded tests ask that no Go source
// carries the document as a literal and that every copy of the document in the
// tree is pulled in at build time; the two controls ask that there is still
// exactly one of it and that it still validates what it validated before.
//
// "Every copy" rather than "some copy" is the whole of the second test. A tree
// that copies the document into the package that embeds it and leaves the
// original where it was has not retired anything: it has two documents that
// have to be kept in step by hand, which is the defect, and one of them is in
// the build.
//
// The literal test uses a size threshold rather than a text match, so a
// reformatted paste is caught as surely as a byte-identical one. It is
// measured: the pasted schema is an 18 204-byte literal, the largest literal
// anywhere else in this repository is 1 301 bytes, and the threshold is 4 096.
package schema

import (
	"encoding/json"
	"go/ast"
	"go/parser"
	"go/token"
	"os"
	"path"
	"path/filepath"
	"strings"
	"testing"

	libschema "github.com/codedellemc/libstorage/api/utils/schema"
)

// maxLiteral is the largest string literal this repository has a use for.
const maxLiteral = 4096

// repoRoot is the directory above this package: the probe is copied into a
// directory at the root of the candidate's tree, and `go test` runs here.
func repoRoot(t *testing.T) string {
	t.Helper()
	wd, err := os.Getwd()
	if err != nil {
		t.Fatal(err)
	}
	return filepath.Dir(wd)
}

// walkSource calls fn for every non-vendored file under the repository root
// whose name ends in ext.
func walkSource(t *testing.T, ext string, fn func(path string)) {
	t.Helper()
	root := repoRoot(t)
	err := filepath.Walk(root, func(p string, fi os.FileInfo, err error) error {
		if err != nil {
			return nil
		}
		if fi.IsDir() {
			switch fi.Name() {
			case "vendor", ".git", "testdata":
				return filepath.SkipDir
			}
			return nil
		}
		if strings.HasSuffix(p, ext) {
			fn(p)
		}
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}
}

// schemaFiles returns every file in the tree that is the libStorage API
// schema: a JSON object whose `definitions` describe this API's resources.
func schemaFiles(t *testing.T) []string {
	t.Helper()
	var found []string
	walkSource(t, ".json", func(p string) {
		b, err := os.ReadFile(p)
		if err != nil {
			return
		}
		var doc struct {
			Definitions map[string]json.RawMessage `json:"definitions"`
		}
		if json.Unmarshal(b, &doc) != nil {
			return
		}
		for _, want := range []string{"volume", "snapshot",
			"volumeCreateRequest"} {
			if _, ok := doc.Definitions[want]; !ok {
				return
			}
		}
		found = append(found, p)
	})
	return found
}

// TestNoGoSourceCarriesTheSchemaAsALiteral is the milestone: the generated
// paste is gone and nothing has replaced it with another one.
func TestNoGoSourceCarriesTheSchemaAsALiteral(t *testing.T) {
	var big []string
	walkSource(t, ".go", func(p string) {
		fset := token.NewFileSet()
		f, err := parser.ParseFile(fset, p, nil, 0)
		if err != nil {
			return
		}
		ast.Inspect(f, func(n ast.Node) bool {
			bl, ok := n.(*ast.BasicLit)
			if ok && bl.Kind == token.STRING && len(bl.Value) > maxLiteral {
				rel, _ := filepath.Rel(repoRoot(t), p)
				big = append(big, rel)
				return false
			}
			return true
		})
	})
	if len(big) > 0 {
		t.Errorf("Go source still carries a document-sized string literal: %v",
			big)
	}
}

// TestTheSchemaIsPulledInAtBuildTime asks that the document reaches the program
// without anyone running a generator, and asks it of every copy of the document
// the tree holds.
func TestTheSchemaIsPulledInAtBuildTime(t *testing.T) {
	files := schemaFiles(t)
	if len(files) == 0 {
		t.Fatal("no file in the tree is the libStorage API schema")
	}
	root := repoRoot(t)
	var orphan []string
	for _, sf := range files {
		if !embedded(t, root, sf) {
			rel, _ := filepath.Rel(root, sf)
			orphan = append(orphan, rel)
		}
	}
	if len(orphan) > 0 {
		t.Errorf("no Go source pulls the schema in at build time: %v", orphan)
	}
}

// embedded reports whether some Go file in the document's own directory, or in
// a directory above it and inside the repository, carries a `//go:embed` line
// that names the document. An embed pattern may name a file in a subdirectory
// of the package that declares it, so the search walks upwards rather than
// looking only beside the file.
func embedded(t *testing.T, root, doc string) bool {
	t.Helper()
	for dir := filepath.Dir(doc); strings.HasPrefix(dir, root); dir = filepath.Dir(dir) {
		rel, err := filepath.Rel(dir, doc)
		if err == nil {
			for _, pat := range embedPatterns(t, dir) {
				if patternCovers(pat, filepath.ToSlash(rel)) {
					return true
				}
			}
		}
		if dir == root {
			break
		}
	}
	return false
}

// embedPatterns returns every pattern named by a `//go:embed` line in a Go file
// directly inside dir.
func embedPatterns(t *testing.T, dir string) []string {
	t.Helper()
	entries, err := os.ReadDir(dir)
	if err != nil {
		return nil
	}
	var pats []string
	for _, e := range entries {
		if e.IsDir() || !strings.HasSuffix(e.Name(), ".go") {
			continue
		}
		fset := token.NewFileSet()
		f, err := parser.ParseFile(fset, filepath.Join(dir, e.Name()), nil,
			parser.ParseComments)
		if err != nil {
			continue
		}
		for _, cg := range f.Comments {
			for _, c := range cg.List {
				if !strings.HasPrefix(c.Text, "//go:embed") {
					continue
				}
				pats = append(pats,
					strings.Fields(strings.TrimPrefix(c.Text, "//go:embed"))...)
			}
		}
	}
	return pats
}

// patternCovers reports whether a `//go:embed` pattern names rel, either
// directly or by naming a directory rel sits under. It is `path.Match` plus the
// directory rule, which is what the embed patterns this repository could
// plausibly use amount to.
func patternCovers(pat, rel string) bool {
	pat = strings.Trim(pat, `"`+"`")
	pat = strings.TrimPrefix(pat, "all:")
	if pat == "" {
		return false
	}
	if ok, _ := path.Match(pat, rel); ok {
		return true
	}
	for p := path.Dir(rel); p != "." && p != "/"; p = path.Dir(p) {
		if ok, _ := path.Match(pat, p); ok {
			return true
		}
	}
	return false
}

// TestTheSchemaExistsExactlyOnce is a control. One copy is the point; two is
// the defect this milestone retires, whichever two they are.
func TestTheSchemaExistsExactlyOnce(t *testing.T) {
	files := schemaFiles(t)
	if len(files) != 1 {
		root := repoRoot(t)
		rels := make([]string, 0, len(files))
		for _, f := range files {
			rel, _ := filepath.Rel(root, f)
			rels = append(rels, rel)
		}
		t.Errorf("the tree holds %d copies of the API schema, want 1: %v",
			len(files), rels)
	}
}

// TestValidationAcceptsAndRejects is a control, and the one that proves the
// schema is reachable rather than merely present: every resource schema is a
// `$ref` into the document, so a document that does not load makes both of
// these fail.
func TestValidationAcceptsAndRejects(t *testing.T) {
	good := []byte(`{"id":"vol-000","name":"Volume 000","size":378}`)
	if err := libschema.Validate(nil, libschema.VolumeSchema, good); err != nil {
		t.Errorf("a well-formed volume was rejected: %v", err)
	}
	for _, bad := range [][]byte{
		[]byte(`{"name":"no id"}`),
		[]byte(`{"id":"vol-000","name":"Volume 000","size":"not a number"}`),
	} {
		if err := libschema.Validate(nil, libschema.VolumeSchema, bad); err == nil {
			t.Errorf("a malformed volume was accepted: %s", bad)
		}
	}
}
