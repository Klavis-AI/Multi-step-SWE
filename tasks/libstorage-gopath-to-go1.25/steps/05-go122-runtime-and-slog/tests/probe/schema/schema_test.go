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
// be spelled any particular way. The three graded tests ask that no Go source
// carries the document as a literal, that every copy of the document in the
// tree is pulled in at build time, and that no build rule still reads it; the
// two controls ask that there is still exactly one of it and that it still
// validates what it validated before.
//
// "Every copy" rather than "some copy" is the whole of the build-time test. A
// tree that copies the document into the package that embeds it and leaves the
// original where it was has not retired anything: it has two documents that
// have to be kept in step by hand, which is the defect, and one of them is in
// the build.
//
// Build time means the build. The embed patterns are read out of `go/build`
// rather than out of the comment text of every `.go` file beside the document,
// because a `//go:embed` line is just as visible in a file the compiler never
// compiles: `//go:build ignore`, a `mock` tag, another GOOS. go/build applies
// the same constraints the compiler does, and it keeps a `_test.go` file's
// patterns in a separate field that is deliberately not consulted here -- the
// API's schema has to reach the program, not the test binary.
//
// The rule test is the other half of "the generated file is gone and so is the
// rule that wrote it". A tree that deletes the paste and leaves the recipe
// behind has kept the thing that will write it again, so every makefile in the
// tree is read and one that still names the document fails.
//
// The literal test uses a size threshold rather than a text match, so a
// reformatted paste is caught as surely as a byte-identical one, and it asks
// the question three ways because a document need not arrive in one piece: one
// literal longer than the threshold, one `+` chain of literals longer than it,
// or one file whose pieces add up past it. The threshold is measured: the
// pasted schema is an 18 204-byte literal, the largest other literal anywhere
// in this repository is 1 301 bytes, and the line is drawn at 4 096. The third
// reading counts only literals of at least 64 bytes, for the reason recorded
// on `pieceBytes` -- this repository has honest files holding thousands of
// bytes of short strings, and a piece of a document is not a short string.
package schema

import (
	"encoding/json"
	"fmt"
	"go/ast"
	"go/build"
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

// minPiece is the smallest string literal that reads as a piece of a document
// rather than as a name, a key, a message or a format. See pieceBytes.
const minPiece = 64

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
		reason := documentSized(f)
		if reason == "" {
			return
		}
		rel, _ := filepath.Rel(repoRoot(t), p)
		big = append(big, rel+" ("+reason+")")
	})
	if len(big) > 0 {
		t.Errorf("Go source still carries a document-sized string literal: %v",
			big)
	}
}

// documentSized reports the first of the three shapes a pasted document takes
// in one file, or "" for a file that carries none of them. One literal is what
// the generated file is; the other two are what it becomes when somebody cuts
// it up, and a document cut into five pieces is still the document.
func documentSized(f *ast.File) string {
	reason := ""
	ast.Inspect(f, func(n ast.Node) bool {
		if reason != "" {
			return false
		}
		switch node := n.(type) {
		case *ast.BasicLit:
			if node.Kind == token.STRING && len(node.Value) > maxLiteral {
				reason = fmt.Sprintf("one %d-byte literal", len(node.Value))
			}
		case *ast.BinaryExpr:
			// Reported at the concatenation rather than at its operands, and
			// not descended into again once it is: the bytes below have been
			// accounted for here.
			if sum, ok := concatLen(node); ok && sum > maxLiteral {
				reason = fmt.Sprintf("%d bytes of concatenated literals", sum)
			}
		}
		return reason == ""
	})
	if reason != "" {
		return reason
	}
	if total := pieceBytes(f); total > maxLiteral {
		return fmt.Sprintf("%d bytes of string literals in one file", total)
	}
	return ""
}

// concatLen sums the string literals of an addition whose operands are all
// string literals, and reports false for any other shape. `+` between two
// literals is the concatenation the compiler folds away, so five 4 000-byte
// pieces joined that way are one 20 000-byte constant written out long.
func concatLen(expr ast.Expr) (int, bool) {
	switch e := expr.(type) {
	case *ast.ParenExpr:
		return concatLen(e.X)
	case *ast.BasicLit:
		if e.Kind != token.STRING {
			return 0, false
		}
		return len(e.Value), true
	case *ast.BinaryExpr:
		if e.Op != token.ADD {
			return 0, false
		}
		x, ok := concatLen(e.X)
		if !ok {
			return 0, false
		}
		y, ok := concatLen(e.Y)
		if !ok {
			return 0, false
		}
		return x + y, true
	}
	return 0, false
}

// pieceBytes is the weight of the document-sized *pieces* in one file: every
// string literal of at least minPiece bytes anywhere inside it, added up. This
// is the reading that catches a const block of five chunks, which no
// per-literal threshold can see, and equally five chunks written as five
// declarations beside each other that a sixth joins back together -- the join
// is in neither of the shapes above, since it need not be a `+` tree and need
// not sit in the same declaration as the pieces, so what is counted here is
// the pieces rather than the expression that reassembles them.
//
// One file rather than one declaration because sibling declarations are one
// edit away from a const block. One file rather than one package, though a
// package is the wider net, because the largest honest directory total in this
// repository is 2 731 bytes against the same 4 096 -- a per-package reading
// would fit too -- and a document spread over two files has stopped being the
// shape this milestone is about, which is one Go file restating it. The file
// is also the unit this walk already parses.
//
// The floor is the whole of what makes it readable against this repository, and
// it is measured rather than guessed. Adding up every literal instead reports
// 7 651 bytes for `api/server/server_names.go`, a map of about a thousand
// adjectives, nouns and countries, and 4 727 for the ginkgo suite in
// `api/utils/utils_paths_test.go`: both honest, both above the threshold,
// neither a document. With the floor at 64 the largest honest file in this
// repository is `api/utils/schema/schema_test.go` at 2 635 bytes, then
// `cli/lss/lss.go` at 2 022 and `api/tests/tests_tls.go` at 1 004, all
// comfortably under; the generated paste this milestone retires weighs 18 204
// under the same reading. A document cut into pieces small enough to slip
// beneath the floor has to be cut into more than two hundred and eighty of
// them, and one cut into pieces the floor keeps has to be spread over at least
// five files to stay under the total as well.
func pieceBytes(node ast.Node) int {
	total := 0
	ast.Inspect(node, func(n ast.Node) bool {
		lit, ok := n.(*ast.BasicLit)
		if ok && lit.Kind == token.STRING && len(lit.Value) >= minPiece {
			total += len(lit.Value)
		}
		return true
	})
	return total
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

// embedded reports whether the package in the document's own directory, or in a
// directory above it and inside the repository, compiles a `//go:embed` pattern
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

// embedPatterns returns the `//go:embed` patterns the package in dir actually
// compiles. go/build applies the same build constraints the compiler does, so a
// directive in a file this platform does not build is not among them -- which
// is the reason for asking go/build rather than reading comment text, since a
// `//go:build ignore` file, a `mock`-tagged file and a file for another GOOS
// all carry their directive just as visibly as a compiled one and none of them
// puts the document in the program. The patterns of `_test.go` files are left
// out for the same reason: go/build keeps those in TestEmbedPatterns and
// XTestEmbedPatterns, and the API's schema has to reach the program rather than
// the test binary.
//
// A directory with no Go files in it, and one holding more than one package,
// both come back as an error beside a package filled in as far as go/build got
// with it. The patterns are used when there are any, and an error with none of
// them is a directory that embeds nothing.
func embedPatterns(t *testing.T, dir string) []string {
	t.Helper()
	pkg, err := build.ImportDir(dir, 0)
	if pkg == nil || (err != nil && len(pkg.EmbedPatterns) == 0) {
		return nil
	}
	return pkg.EmbedPatterns
}

// patternCovers reports whether a `//go:embed` pattern names rel, either
// directly or by naming a directory rel sits under. go/build has already done
// the lexical half -- it strips the quoting and splits a directive that carries
// several patterns into several -- so what is left here is the two rules it
// does not apply: the `all:` prefix, which changes which files under a
// directory are selected and not whether the directory is named, and the rule
// that a pattern naming a directory covers what is under it. The rest is
// `path.Match`, which is what the embed patterns this repository could
// plausibly use amount to.
func patternCovers(pat, rel string) bool {
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

// TestNoBuildRuleStillReadsTheSchemaDocument is the other half of "the
// generated file is gone and so is the rule that wrote it". A tree that deletes
// the paste and leaves the recipe that produced it has kept the thing that will
// produce it again, and the next person to run `make` gets the second copy back
// with no warning that anything happened.
//
// It is stated over the document rather than over the generated file, because
// the document is what the rule has to read: the recipe can write its output
// anywhere, but a rule that pastes this schema into Go source has to name the
// schema.
func TestNoBuildRuleStillReadsTheSchemaDocument(t *testing.T) {
	docs := schemaFiles(t)
	if len(docs) == 0 {
		t.Fatal("no file in the tree is the libStorage API schema")
	}
	names := make(map[string]bool, len(docs))
	for _, doc := range docs {
		names[filepath.Base(doc)] = true
	}
	root := repoRoot(t)
	var rules []string
	// Every file, because the filter is the base name and not the extension:
	// make reads three names of its own and this repository's `.mk` fragments.
	walkSource(t, "", func(p string) {
		if !isMakefile(filepath.Base(p)) {
			return
		}
		b, err := os.ReadFile(p)
		if err != nil {
			return
		}
		rel, _ := filepath.Rel(root, p)
		for i, line := range strings.Split(string(b), "\n") {
			// A `#` outside a quoted string starts a comment. Cutting at the
			// first `#` treats one inside quotes as a comment too, which reads
			// a shade more of a line as commentary than make does; no rule in
			// this repository puts a `#` inside a file name, and the
			// difference is not worth a make lexer here.
			text, _, _ := strings.Cut(line, "#")
			if !namesOneOf(text, names) {
				continue
			}
			rules = append(rules, fmt.Sprintf("%s:%d: %s", rel, i+1,
				strings.TrimSpace(line)))
		}
	})
	if len(rules) > 0 {
		t.Errorf("a build rule still reads the schema document: %v", rules)
	}
}

// isMakefile reports whether make would read a file of this name: the three
// names it looks for on its own, and the `.mk` fragments this repository
// includes into them.
func isMakefile(name string) bool {
	switch name {
	case "Makefile", "makefile", "GNUmakefile":
		return true
	}
	return strings.HasSuffix(name, ".mk")
}

// namesOneOf reports whether a makefile line names one of the documents. The
// comparison is against a whole path element rather than against a substring,
// so the directory `./api/utils/schema` that this repository's test rules
// recurse into does not read as a document inside it, and a file called
// `mylibstorage.json` does not read as `libstorage.json`. Everything that
// cannot appear in a file name separates one path from the next, which is
// coarse enough to survive a `:=`, a `$(VAR)` and a trailing backslash without
// knowing what any of them mean.
func namesOneOf(line string, names map[string]bool) bool {
	paths := strings.Map(func(r rune) rune {
		switch {
		case r >= 'a' && r <= 'z', r >= 'A' && r <= 'Z', r >= '0' && r <= '9':
			return r
		case r == '.', r == '-', r == '_', r == '/':
			return r
		}
		return ' '
	}, line)
	for _, field := range strings.Fields(paths) {
		for _, elem := range strings.Split(field, "/") {
			if names[elem] {
				return true
			}
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
