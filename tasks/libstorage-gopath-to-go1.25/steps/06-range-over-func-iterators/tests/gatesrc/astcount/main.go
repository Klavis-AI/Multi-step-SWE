// Command astcount reports the counts the migration gates are stated in terms
// of: suppression sites, the size of the test corpus, hand-written sort
// adapters, and the legacy build-constraint and import constructs each step
// retires.
//
//	astcount [-root <dir>] <dir>...
//
// Each argument is one package directory; only the `.go` files directly in it
// are read, so the caller decides the scope by choosing the directories (the
// gates derive them from `go list`, never from a path list). Output is one
// JSON object on stdout.
//
// `-root` names the repository the per-file maps are keyed against, so that a
// key is the path a gate specification would write down rather than wherever
// the scorer happened to unpack the tree. Without it the keys are the paths as
// they were given on the command line.
//
// Counting is done over the parsed syntax, not over lines of text, because the
// gates are stated in expressions and sites rather than blocks and files: a
// suppression moved onto a shared line is still one suppression, and a test
// merged into a table is still one case.
package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"go/ast"
	"go/build/constraint"
	"go/parser"
	"go/token"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
)

// counts is the whole report. Every field is a count of syntax positions.
type counts struct {
	// Anti-weakening.
	Suppressions int `json:"suppressions"`
	Skips        int `json:"skips"`
	TestFuncs    int `json:"test_funcs"`
	Specs        int `json:"specs"`
	Assertions   int `json:"assertions"`
	TestFiles    int `json:"test_files"`
	// ConstrainedTestFiles counts the `*_test.go` files that carry a build
	// constraint at all -- a `//go:build` line, a `// +build` line, or a
	// GOOS/GOARCH filename suffix. A test file that grows a constraint stops
	// being compiled, which is a skip no skip-counter can see: the package
	// reports `[no test files]` instead of a failure. Detection is
	// `constraintValue`'s, the same reading the per-file map below is built
	// from, so "constrained" means one thing in this census.
	ConstrainedTestFiles int `json:"constrained_test_files"`
	// UnconstrainedSrcFiles counts the non-test files that carry no build
	// constraint at all, and it is a floor rather than a ceiling. A file that
	// grows a constraint stops being compiled on this platform, which is how a
	// package that will not build is made to build: tag the offending file out
	// and `go build ./...`, `go vet ./...` and `gofmt` all stop seeing it. The
	// per-file map below does not catch that on its own -- it is a bound on
	// loss, and a key the candidate adds is deliberately not held against it,
	// because adding a platform file is not a regression. Counting the files
	// that are *not* constrained separates the two: a new unconstrained file
	// raises this count and a min floor ignores that, while tagging an
	// existing one out lowers it. Deleting a source file lowers it too, which
	// is also correct.
	UnconstrainedSrcFiles int `json:"unconstrained_src_files"`
	// BlankDiscards counts the statements that throw a returned value away by
	// assigning it to `_`: an assignment or a short variable declaration with
	// at least one blank on the left whose right-hand side reaches a call. The
	// name does not say the value is an error, because the syntax does not say
	// what a call returns; what it does say is that something was produced and
	// then dropped, which is `x, _ := f()` -- the ordinary way to discard an
	// error -- as much as `_ = f()`. A right-hand side with no call in it,
	// `_ = 1`, computed nothing to discard and does not count.
	BlankDiscards int `json:"blank_discards"`

	// Step-scoped constructs.
	LegacyBuildTags int `json:"legacy_build_tags"`
	GoBuildTags     int `json:"go_build_tags"`
	// ConvertedBuildTags counts *files* that carry a `//go:build` constraint
	// and no `// +build` line beside it. Counting the two comment forms
	// separately is not enough: `gofmt` writes the `//go:build` line for any
	// file that has only the legacy one and leaves both in place, so a tree
	// that has been through nothing but gofmt has a full count of the modern
	// form while still carrying every legacy line. This field is the one that
	// distinguishes converted from merely reformatted -- and from deleted,
	// since a file whose constraint was dropped has neither form.
	ConvertedBuildTags int `json:"converted_build_tags"`
	GoGenerate         int `json:"go_generate"`
	SortAdapters       int `json:"sort_adapters"`
	SortSortCalls      int `json:"sort_sort_calls"`
	ChanReturns        int `json:"chan_returns"`
	GoStmts            int `json:"go_stmts"`
	TimeSleeps         int `json:"time_sleeps"`
	SetFinalizer       int `json:"set_finalizer"`
	// GetenvGOPATH counts the syntax positions at which the repository's Go
	// source names the GOPATH environment variable. It is wider than one
	// function name because what the migration removes is the dependency on
	// the variable, not one spelling of the call that fetches it. Three
	// positions count, each exactly once even where a literal answers to more
	// than one description:
	//
	//   - a string literal whose value is exactly "GOPATH", wherever it stands
	//     -- a call argument, a const or var declaration, a map key, a slice
	//     element, a struct tag -- so that binding the name to a constant
	//     first, `const k = "GOPATH"; os.Getenv(k)`, does not hide it;
	//   - a string literal that interpolates the variable, "$GOPATH" or
	//     "${GOPATH}" anywhere inside it, which is how os.ExpandEnv and a
	//     shell command line ask for the same value;
	//   - go/build's own GOPATH field, which reads the variable through the
	//     toolchain rather than through the environment.
	//
	// What no syntax census can reach is a value assembled at run time: "GO" +
	// "PATH", or a scan of os.Environ() for a name computed elsewhere. Those
	// are the behavior family's to catch, and it does catch them, because
	// outside a GOPATH workspace the fixture path such code builds does not
	// exist and the specs that need those fixtures fail.
	GetenvGOPATH   int `json:"getenv_gopath"`
	EmptyIfaceExpr int `json:"empty_iface_expr"`

	// GenericDecls counts the declarations that carry type parameters: a
	// function declaration whose signature has a type parameter list, and a
	// type declaration that has one. Declarations rather than parameters, so
	// `func F[K comparable, V any](m map[K]V) []K` is one and not two -- what
	// the milestone asks is that a declaration was made generic, and a second
	// parameter on it is a fact about the shape of that declaration rather than
	// more of the work. A method on a generic type is not one either: its own
	// signature carries no type parameter list, only its receiver names one,
	// and counting the methods as well would price a single generic type at
	// however many methods happen to hang off it.
	//
	// What this does not prove is the milestone. A type parameter existing is
	// no evidence that the duplication it was meant to remove is gone -- a tree
	// can declare a generic helper beside the clones it was supposed to replace
	// and leave every one of them exactly where it was. Measuring that is
	// dupscan's job, and dupscan is graded separately and more heavily. This
	// counter closes the one case dupscan cannot speak to: a tree where the
	// scanner reports nothing because the clone groups were broken up some
	// other way -- deleted, forwarded by hand, or reshaped until it no longer
	// recognises them -- and no generic declaration exists anywhere either.
	GenericDecls int `json:"generic_decls"`

	// WrapVerbs is one per `%w` in a `fmt.Errorf` format literal -- per verb
	// rather than per call, because one message can carry two causes and a
	// migration that joins two errors has done the work twice. It is the one
	// syntactic mark a wrapped cause leaves: `%w` and `%v` render an error
	// identically, so nothing about the produced text distinguishes them and
	// the difference is only visible here and to a type checker.
	//
	// UnwrapMethods is one per `Unwrap` method declaration -- the other half
	// of the same contract, written by hand where a type carries its cause in
	// a field instead of in a format string. Both spellings are counted,
	// `Unwrap() error` and `Unwrap() []error`; a receiver is required, so a
	// package-level helper called Unwrap is not one.
	WrapVerbs     int `json:"wrap_verbs"`
	UnwrapMethods int `json:"unwrap_methods"`

	// LoopVarCopyWorkarounds is one per function literal written inside a loop
	// that declares its own variables, where the literal either takes a
	// parameter named after one of them or is invoked with one of them as an
	// argument. That is the pre-1.22 workaround written out: the parameter and
	// the argument exist only to copy the shared loop variable before the
	// literal outlives the iteration. Once the loop variable is per-iteration
	// the copy says nothing, and the count is how many of them are still
	// there. Either half alone is enough to be counted, because either half
	// alone is the idiom still in the tree: renaming the parameter leaves the
	// argument doing the copying, and a parameter named after the loop
	// variable is the shape a reader has to think about whatever it is passed.
	//
	// The other side of the idiom -- a literal that is handed what it needs
	// and reaches past the parameter for the loop variable anyway -- is not
	// counted here. Deciding whether a parameter stands in for a loop
	// variable needs the parameter's type, and a name-based census answers
	// that question wrongly in both directions; the typed loop scanner beside
	// this tool does it instead.
	LoopVarCopyWorkarounds int `json:"loopvar_copy_workarounds"`

	// KeySortLoops is one per accumulate-then-sort site: a range loop whose
	// body does nothing but append one of its own loop variables to a slice,
	// followed later in the same statement list by a call that sorts that
	// slice in place. That pair is what `slices.Sorted(maps.Keys(m))` and
	// `slices.Sorted(slices.Values(s))` became in Go 1.23, and this counts the
	// ones still written out longhand.
	//
	// The reading is deliberately narrow. The body has to be the single
	// append and nothing else, because a loop that also filters or transforms
	// is not the idiom the iterator adapters replace; the appended expression
	// has to be a loop variable rather than anything computed from one, for
	// the same reason; and the sort has to name the same slice, so that two
	// unrelated collections in one function are not read as one site. What it
	// does not reach is a pair split across a block boundary -- an append loop
	// in a function and the sort in its caller -- which is not a shape this
	// repository writes.
	KeySortLoops int `json:"key_sort_loops"`

	// CollectLoops is the wider count KeySortLoops is a subset of: one per
	// range loop whose body does nothing but append one of its own loop
	// variables to a slice, whether or not the slice is sorted afterwards.
	// That loop is what `slices.Collect`, `slices.Sorted` and
	// `slices.AppendSeq` express in one call from Go 1.23, over whichever
	// sequence the range was reading -- `maps.Keys`, `maps.Values` or
	// `slices.Values`.
	CollectLoops int `json:"collect_loops"`

	// PathValueCalls is one per call to a `PathValue` method, whatever it is
	// called on. The routing this repository has always used reads its path
	// variables out of a map a third-party library puts on the request; the
	// standard library grew its own accessor in Go 1.22 and it is a method on
	// the request rather than a package-level function, so there is no
	// package-qualified selector to count and the receiver is whatever the
	// handler happened to name its request.
	PathValueCalls int `json:"pathvalue_calls"`

	// Import-path counts, keyed by the import path.
	Imports map[string]int `json:"imports"`

	// Selector counts for the packages a step retires, keyed by
	// "pkgname.Ident" as written.
	Selectors map[string]int `json:"selectors"`

	// BuildConstraints is the identity of every constrained file, keyed by its
	// path under `-root`. Where the two build-tag counts above grade the form
	// the constraint is written in, this records what the constraint says and
	// which files carry one at all, so that converting a file moves nothing
	// and deleting one -- or quietly widening what a platform builds -- moves
	// this. Files constrained by their name alone are in here too; they carry
	// no comment for a count to reach.
	BuildConstraints map[string]string `json:"build_constraints"`

	Files int `json:"files"`
}

// suppressionRE matches every comment form that silences a checker in this
// toolchain's ecosystem. A gate the agent can quiet with a comment is not a
// gate, so all of them are counted and all of them are gated at the baseline
// value (zero).
var suppressionRE = regexp.MustCompile(
	`//\s*(nolint|lint:ignore|lint:file-ignore|staticcheck:ignore|` +
		`revive:disable|golangci|gosec:disable|deadcode:ignore)|#nosec`)

// legacyBuildRE is the pre-1.17 constraint syntax; goBuildRE is its
// replacement. Both are comments, so neither is visible in the AST as anything
// but a comment, and both are counted per line.
var (
	legacyBuildRE = regexp.MustCompile(`^//\s*\+build\b`)
	goBuildRE     = regexp.MustCompile(`^//go:build\b`)
	generateRE    = regexp.MustCompile(`^//go:generate\b`)
)

// knownOS and knownArch are go/build's own tables of past, present and future
// GOOS and GOARCH values, which is what the filename half of a build constraint
// is read against. They are written out rather than asked for because the rule
// belongs to the language and not to the machine: go/build exposes the match
// only through a Context bound to the platform it is running on, and this
// census has to see a darwin file from linux.
var (
	knownOS = map[string]bool{
		"aix": true, "android": true, "darwin": true, "dragonfly": true,
		"freebsd": true, "hurd": true, "illumos": true, "ios": true,
		"js": true, "linux": true, "nacl": true, "netbsd": true,
		"openbsd": true, "plan9": true, "solaris": true, "wasip1": true,
		"windows": true, "zos": true,
	}
	knownArch = map[string]bool{
		"386": true, "amd64": true, "amd64p32": true, "arm": true,
		"armbe": true, "arm64": true, "arm64be": true, "loong64": true,
		"mips": true, "mipsle": true, "mips64": true, "mips64le": true,
		"mips64p32": true, "mips64p32le": true, "ppc": true, "ppc64": true,
		"ppc64le": true, "riscv": true, "riscv64": true, "s390": true,
		"s390x": true, "sparc": true, "sparc64": true, "wasm": true,
	}
)

// selectorsOfInterest are the package-qualified identifiers whose disappearance
// (or appearance) a step is scored on. Counting the selector rather than the
// import catches a file that keeps the import for one call and claims the rest
// were removed.
var selectorsOfInterest = map[string]bool{
	"ioutil": true, "goof": true, "logrus": true, "mux": true,
	"gournal": true, "apictx": true, "rand": true, "slog": true,
	"slices": true, "maps": true, "cmp": true, "iter": true,
	"unique": true, "weak": true, "synctest": true, "goleak": true,
	"errors": true, "context": true, "sort": true,
}

// aliasedPkgs are the packages whose selectors are resolved through the file's
// import list rather than by their leftmost identifier. A file that writes
// `import sl "slices"` calls `sl.Collect`, which is the same call the gates
// read as `slices.Collect`. For these four the import path is also the package
// name, so the alias can be undone from the import list alone. No other
// package is resolved this way: the leftmost identifier is what the gates that
// name them are written against, and a path whose last element is not the
// package name would need more than the import list to resolve.
var aliasedPkgs = map[string]bool{
	"slices": true, "maps": true, "iter": true, "unique": true,
}

// specNames are the ginkgo/gomega spec constructors; assertNames are the
// assertion entry points. A migration that deletes specs to pass loses here.
//
// skipNames and pendingNames are the two ways a spec or a test stops running
// without being deleted. pendingNames are ginkgo v1's pending constructors:
// each pushes its node with types.FlagTypePending, so the spec is still
// registered and its body never runs, which is a skip written as a
// declaration. They are deliberately not in specNames, so rewriting It as PIt
// spends a spec to buy a skip and both bounds see it. The focus constructors
// -- FIt, FDescribe, FContext, FSpecify, FMeasure -- are as deliberately in
// neither set: RunSpecsWithCustomReporters exits GINKGO_FOCUS_EXIT_CODE when a
// suite passes with specs focused, so go test reports the package FAIL and
// narrowing a suite that way cannot buy a green run.
var (
	specNames = map[string]bool{
		"It": true, "Describe": true, "Context": true, "Specify": true,
		"Entry": true, "DescribeTable": true, "When": true,
	}
	skipNames = map[string]bool{
		"Skip": true, "Skipf": true, "SkipNow": true,
	}
	pendingNames = map[string]bool{
		"PIt": true, "XIt": true,
		"PDescribe": true, "XDescribe": true,
		"PContext": true, "XContext": true,
		"PSpecify": true, "XSpecify": true,
		"PMeasure": true, "XMeasure": true,
	}
	assertNames = map[string]bool{
		"Expect": true, "Ω": true, "Expectf": true,
	}
	assertSelectors = map[string]bool{
		"assert": true, "require": true,
	}
	fatalNames = map[string]bool{
		"Error": true, "Errorf": true, "Fatal": true, "Fatalf": true,
	}
)

func main() {
	root := flag.String("root", "",
		"repository root the per-file map keys are written relative to")
	flag.Parse()
	c := counts{Imports: map[string]int{}, Selectors: map[string]int{},
		BuildConstraints: map[string]string{}}
	fset := token.NewFileSet()
	for _, dir := range flag.Args() {
		entries, err := os.ReadDir(dir)
		if err != nil {
			continue // a package listed but not readable is not a count
		}
		for _, e := range entries {
			name := e.Name()
			if e.IsDir() || !strings.HasSuffix(name, ".go") {
				continue
			}
			path := filepath.Join(dir, name)
			key := path
			if *root != "" {
				if rel, err := filepath.Rel(*root, path); err == nil {
					key = rel
				}
			}
			f, err := parser.ParseFile(fset, path, nil, parser.ParseComments)
			if err != nil {
				// An unparseable file is still evidence: count it as a file so
				// deleting the tree cannot look like a clean tree.
				c.Files++
				continue
			}
			c.Files++
			if strings.HasSuffix(name, "_test.go") {
				c.TestFiles++
			}
			scanFile(&c, f, key, name)
		}
	}
	out, err := json.MarshalIndent(c, "", "  ")
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	fmt.Println(string(out))
}

// scanFile adds one file's counts to c. `key` is how the file is named in the
// per-file maps and `name` is its base name, which is half of its build
// constraint.
func scanFile(c *counts, f *ast.File, key, name string) {
	legacy, modern := false, false
	var plusBuild, goBuild []string
	for _, group := range f.Comments {
		for _, comment := range group.List {
			text := comment.Text
			if suppressionRE.MatchString(text) {
				c.Suppressions++
			}
			if legacyBuildRE.MatchString(text) {
				c.LegacyBuildTags++
				legacy = true
				plusBuild = append(plusBuild, text)
			}
			if goBuildRE.MatchString(text) {
				c.GoBuildTags++
				modern = true
				goBuild = append(goBuild, text)
			}
			if generateRE.MatchString(text) {
				c.GoGenerate++
			}
		}
	}
	if modern && !legacy {
		c.ConvertedBuildTags++
	}
	if value := constraintValue(name, goBuild, plusBuild); value != "" {
		c.BuildConstraints[key] = value
		if strings.HasSuffix(name, "_test.go") {
			c.ConstrainedTestFiles++
		}
	} else if !strings.HasSuffix(name, "_test.go") {
		c.UnconstrainedSrcFiles++
	}

	// aliases maps the name a selector in this file is written with to the
	// package it actually names, for the few packages an alias is undone for.
	aliases := map[string]string{}
	for _, imp := range f.Imports {
		path := strings.Trim(imp.Path.Value, `"`)
		c.Imports[path]++
		if !aliasedPkgs[path] {
			continue
		}
		local := path
		if imp.Name != nil {
			local = imp.Name.Name
		}
		if local == "_" || local == "." {
			continue
		}
		aliases[local] = path
	}

	// A type is a sort adapter when the same file declares Len, Less and Swap
	// on it. That is the shape `sort.Sort` requires, and it is what "generated
	// collection code" looks like once the generator is gone.
	methods := map[string]map[string]bool{}

	ast.Inspect(f, func(n ast.Node) bool {
		switch node := n.(type) {
		case *ast.FuncDecl:
			if node.Recv != nil && len(node.Recv.List) == 1 {
				recv := receiverName(node.Recv.List[0].Type)
				if recv != "" {
					if methods[recv] == nil {
						methods[recv] = map[string]bool{}
					}
					methods[recv][node.Name.Name] = true
				}
			}
			if node.Recv == nil && isTestFuncName(node.Name.Name) &&
				node.Name.IsExported() {
				c.TestFuncs++
			}
			if node.Recv != nil && node.Name.Name == "Unwrap" &&
				isUnwrapSignature(node.Type) {
				c.UnwrapMethods++
			}
			if node.Type != nil && node.Type.TypeParams != nil &&
				len(node.Type.TypeParams.List) > 0 {
				c.GenericDecls++
			}
		case *ast.TypeSpec:
			// The other half of the same count, and the half a generic container
			// arrives as: `type Set[T comparable] map[T]struct{}` is one
			// declaration here however many methods are declared on it.
			if node.TypeParams != nil && len(node.TypeParams.List) > 0 {
				c.GenericDecls++
			}
		case *ast.GoStmt:
			c.GoStmts++
		case *ast.RangeStmt:
			if node.Tok == token.DEFINE {
				scanLoop(c, loopVarNames(node.Key, node.Value), node.Body)
			}
		case *ast.ForStmt:
			if init, ok := node.Init.(*ast.AssignStmt); ok &&
				init.Tok == token.DEFINE {
				scanLoop(c, loopVarNames(init.Lhs...), node.Body)
			}
		case *ast.BlockStmt:
			// Statement-list shapes are scanned per block: the idiom is two
			// statements that have to be siblings, so the walk that sees one
			// node at a time cannot recognise it.
			scanKeySortBlock(c, node.List)
		case *ast.InterfaceType:
			if node.Methods == nil || len(node.Methods.List) == 0 {
				c.EmptyIfaceExpr++
			}
		case *ast.Ident:
			if node.Name == "any" {
				// `any` is a predeclared alias, so it is an identifier rather
				// than an InterfaceType node; counting it here keeps the two
				// spellings indistinguishable in the gate.
				c.EmptyIfaceExpr++
			}
		case *ast.FuncType:
			if node.Results != nil {
				for _, r := range node.Results.List {
					if _, ok := r.Type.(*ast.ChanType); ok {
						c.ChanReturns++
					}
				}
			}
		case *ast.SelectorExpr:
			// go/build's Default.GOPATH is the same environment variable,
			// reached through the toolchain's own package instead of through
			// the environment directly.
			if node.Sel.Name == "GOPATH" && rootIdent(node.X) == "build" {
				c.GetenvGOPATH++
			}
		case *ast.BasicLit:
			// Counted here rather than at the call that consumes it: the
			// literal is the site, and a literal reached this way is reached
			// once wherever it was written.
			if litNamesGOPATH(node) {
				c.GetenvGOPATH++
			}
		case *ast.AssignStmt:
			if isBlankDiscard(node) {
				c.BlankDiscards++
			}
		case *ast.CallExpr:
			scanCall(c, node, aliases)
		}
		return true
	})

	for _, m := range methods {
		if m["Len"] && m["Less"] && m["Swap"] {
			c.SortAdapters++
		}
	}
}

// loopVarNames is the set of names a loop declared for itself, blanks
// dropped. Only the `:=` forms reach here: a loop that assigns to variables
// declared somewhere else never had per-iteration semantics to gain.
func loopVarNames(exprs ...ast.Expr) map[string]bool {
	out := map[string]bool{}
	for _, e := range exprs {
		if id, ok := e.(*ast.Ident); ok && id.Name != "_" {
			out[id.Name] = true
		}
	}
	return out
}

// scanLoop counts the copy idiom inside one loop body. Nested loops are each
// scanned in their own turn by the caller's walk, and each turn only looks for
// the names its own loop declared, so a literal that copies the variables of
// two loops is counted once for each of them.
func scanLoop(c *counts, vars map[string]bool, body *ast.BlockStmt) {
	if len(vars) == 0 || body == nil {
		return
	}
	handed := literalsHandedLoopVars(vars, body)
	ast.Inspect(body, func(n ast.Node) bool {
		lit, ok := n.(*ast.FuncLit)
		if !ok {
			return true
		}
		params := paramNames(lit)
		counted := handed[lit]
		for name := range params {
			if vars[name] {
				counted = true
				break
			}
		}
		if counted {
			c.LoopVarCopyWorkarounds++
		}
		return true
	})
}

// literalsHandedLoopVars is the set of function literals inside one loop body
// that are called with one of the loop's own variables as an argument. It is
// the other half of the copy idiom and the half a rename cannot remove: the
// parameter's name is the author's choice, but the argument is the loop
// variable itself, written at the call. Arguments are read through
// parentheses, and only a bare identifier counts -- a loop variable that
// reaches the literal inside a larger expression is being used for its value
// rather than copied for its storage.
func literalsHandedLoopVars(
	vars map[string]bool, body *ast.BlockStmt) map[*ast.FuncLit]bool {

	out := map[*ast.FuncLit]bool{}
	ast.Inspect(body, func(n ast.Node) bool {
		call, ok := n.(*ast.CallExpr)
		if !ok {
			return true
		}
		lit, ok := unparen(call.Fun).(*ast.FuncLit)
		if !ok {
			return true
		}
		for _, arg := range call.Args {
			if id, ok := unparen(arg).(*ast.Ident); ok && vars[id.Name] {
				out[lit] = true
				break
			}
		}
		return true
	})
	return out
}

// unparen strips the parentheses an expression was written inside, which say
// nothing about what it is.
func unparen(e ast.Expr) ast.Expr {
	for {
		p, ok := e.(*ast.ParenExpr)
		if !ok {
			return e
		}
		e = p.X
	}
}

// paramNames is the set of parameter names a function literal declares,
// blanks dropped. A name here shadows anything outside the literal, which is
// what makes the workaround idiom safe and what makes a reference past it
// impossible to write by accident.
func paramNames(lit *ast.FuncLit) map[string]bool {
	out := map[string]bool{}
	if lit.Type == nil || lit.Type.Params == nil {
		return out
	}
	for _, f := range lit.Type.Params.List {
		for _, id := range f.Names {
			if id.Name != "_" {
				out[id.Name] = true
			}
		}
	}
	return out
}

// scanCall counts the call-expression shapes the gates name.
func scanCall(c *counts, call *ast.CallExpr, aliases map[string]string) {
	switch fun := call.Fun.(type) {
	case *ast.Ident:
		if specNames[fun.Name] {
			c.Specs++
		}
		if assertNames[fun.Name] {
			c.Assertions++
		}
		// The suites in this tree alias the ginkgo DSL into package scope
		// (`var Skip = ginkgo.Skip` in api/tests/tests_suite.go and in
		// api/utils/utils_suite_test.go), so the skip an author here writes
		// is a bare identifier and never reaches the selector case below.
		if skipNames[fun.Name] || pendingNames[fun.Name] {
			c.Skips++
		}
	case *ast.SelectorExpr:
		pkg, ok := fun.X.(*ast.Ident)
		if !ok {
			// A method call on something other than a bare identifier: still
			// worth counting when it is a testing failure.
			if fatalNames[fun.Sel.Name] {
				c.Assertions++
			}
			if fun.Sel.Name == "PathValue" {
				c.PathValueCalls++
			}
			return
		}
		// The name the gates read this call by. When this file reached one of
		// the resolved packages through an alias, the alias is undone before
		// anything is counted and the canonical name is the only one
		// recorded: `import maps "slices"` followed by `maps.Collect` is one
		// `slices.Collect` and no `maps.Collect`. Counting the leftmost
		// identifier as well would put that single call into both keys, and
		// the gates that sum selectors would read one call as two. An
		// identifier this file did not import as one of those packages is
		// counted under its own name, unresolved.
		pkgName := pkg.Name
		if canon, ok := aliases[pkg.Name]; ok {
			pkgName = canon
		}
		name := pkgName + "." + fun.Sel.Name
		if selectorsOfInterest[pkgName] {
			c.Selectors[name]++
		}
		if fun.Sel.Name == "PathValue" {
			c.PathValueCalls++
		}
		if name == "fmt.Errorf" && len(call.Args) > 0 {
			c.WrapVerbs += countWrapVerbs(call.Args[0])
		}
		switch {
		case name == "sort.Sort" || name == "sort.Stable":
			c.SortSortCalls++
		case name == "time.Sleep":
			c.TimeSleeps++
		case name == "runtime.SetFinalizer":
			c.SetFinalizer++
		}
		if assertSelectors[pkg.Name] {
			c.Assertions++
		}
		if pkg.Name == "t" && fatalNames[fun.Sel.Name] {
			c.Assertions++
		}
		if specNames[fun.Sel.Name] && pkg.Name == "ginkgo" {
			c.Specs++
		}
		if skipNames[fun.Sel.Name] {
			c.Skips++
		}
		if pendingNames[fun.Sel.Name] && pkg.Name == "ginkgo" {
			c.Skips++
		}
	}
}

// constraintValue is one file's build-constraint identity: what its name
// implies and what its header comments express, in that order, separated by a
// semicolon, either half possibly empty. A file whose name and comments both
// say nothing is unconstrained and has no identity to record.
func constraintValue(name string, goBuild, plusBuild []string) string {
	fromName := filenameConstraint(name)
	fromComment := commentConstraint(goBuild, plusBuild)
	if fromName == "" && fromComment == "" {
		return ""
	}
	// The toolchain ANDs the two halves, so a comment that restates what the
	// name already says selects exactly the platforms the name alone selects.
	// Recording it twice would make a redundant `//go:build amd64` added to
	// lss-linux_amd64.go read as a changed constraint when nothing about what
	// is built has changed, so the restatement is dropped and the file records
	// the value it recorded before.
	if fromComment == fromName {
		fromComment = ""
	}
	return fromName + ";" + fromComment
}

// filenameConstraint returns the constraint a file's name implies, read the way
// go/build reads it: everything before the first underscore is discarded, a
// trailing `_test` is dropped, and what is left has to end in a known GOOS, a
// known GOARCH, or a GOOS followed by a GOARCH. The two-part form is rendered
// the way the equivalent comment would be written.
func filenameConstraint(name string) string {
	base, _, _ := strings.Cut(name, ".")
	i := strings.Index(base, "_")
	if i < 0 {
		return ""
	}
	parts := strings.Split(base[i:], "_")
	if n := len(parts); n > 0 && parts[n-1] == "test" {
		parts = parts[:n-1]
	}
	n := len(parts)
	if n >= 2 && knownOS[parts[n-2]] && knownArch[parts[n-1]] {
		return parts[n-2] + " && " + parts[n-1]
	}
	if n >= 1 && (knownOS[parts[n-1]] || knownArch[parts[n-1]]) {
		return parts[n-1]
	}
	return ""
}

// commentConstraint returns the constraint a file's header comments express,
// rendered from the parsed expression rather than from the comment text. That
// is the whole point of parsing it: `// +build linux` and `//go:build linux`
// are the same constraint written two ways, and this step's conversion rewrites
// every one of them, so a gate on the identity of the constraint has to see
// through the spelling or it would move on work that changed nothing. The
// modern line wins where a file carries both, which is what the toolchain does;
// several legacy lines are ANDed, which is what they mean.
func commentConstraint(goBuild, plusBuild []string) string {
	if len(goBuild) > 0 {
		expr, err := constraint.Parse(goBuild[0])
		if err != nil {
			return ""
		}
		return expr.String()
	}
	var all constraint.Expr
	for _, line := range plusBuild {
		expr, err := constraint.Parse(line)
		if err != nil {
			continue
		}
		if all == nil {
			all = expr
			continue
		}
		all = &constraint.AndExpr{X: all, Y: expr}
	}
	if all == nil {
		return ""
	}
	return all.String()
}

// receiverName returns the bare type name of a method receiver, pointer or not.
func receiverName(expr ast.Expr) string {
	switch t := expr.(type) {
	case *ast.Ident:
		return t.Name
	case *ast.StarExpr:
		return receiverName(t.X)
	case *ast.IndexExpr: // a generic receiver, `func (s Slice[T]) Len() int`
		return receiverName(t.X)
	case *ast.IndexListExpr:
		return receiverName(t.X)
	}
	return ""
}

// isTestFuncName reports whether a top-level function is part of the corpus.
func isTestFuncName(name string) bool {
	for _, prefix := range []string{"Test", "Benchmark", "Fuzz", "Example"} {
		if strings.HasPrefix(name, prefix) {
			return true
		}
	}
	return false
}

// litNamesGOPATH reports whether a string literal names the GOPATH
// environment variable -- either it is the name a lookup is given, or it
// interpolates the variable into a larger string the way os.ExpandEnv and a
// shell command line do.
func litNamesGOPATH(lit *ast.BasicLit) bool {
	if lit.Kind != token.STRING {
		return false
	}
	value := litValue(lit)
	return value == "GOPATH" ||
		strings.Contains(value, "$GOPATH") ||
		strings.Contains(value, "${GOPATH}")
}

// litValue is a string literal's value, quoted or raw. A literal the scanner
// accepted but strconv cannot unquote falls back to trimming the delimiters,
// because a site the census could not decode is still a site.
func litValue(lit *ast.BasicLit) string {
	if value, err := strconv.Unquote(lit.Value); err == nil {
		return value
	}
	return strings.Trim(lit.Value, "\"`")
}

// isUnwrapSignature reports whether a func type is one of the two shapes the
// `errors` package calls to walk a chain: no parameters, and a single result
// that is either `error` or `[]error`. The census reads syntax, so the result
// is matched by how it is written; an error type renamed in the file it is
// declared in is a spelling this cannot follow, and the behaviour family is
// where that tree is caught.
func isUnwrapSignature(ft *ast.FuncType) bool {
	if ft == nil || ft.Results == nil || len(ft.Results.List) != 1 {
		return false
	}
	if ft.Params != nil && len(ft.Params.List) != 0 {
		return false
	}
	switch res := ft.Results.List[0].Type.(type) {
	case *ast.Ident:
		return res.Name == "error"
	case *ast.ArrayType:
		if res.Len != nil {
			return false
		}
		elem, ok := res.Elt.(*ast.Ident)
		return ok && elem.Name == "error"
	}
	return false
}

// countWrapVerbs counts the `%w` verbs in a format argument, when that
// argument is a literal the census can read. A format assembled at run time is
// not counted: there is no literal to count, and a count that guessed would be
// a count of something other than what it names.
func countWrapVerbs(arg ast.Expr) int {
	lit, ok := arg.(*ast.BasicLit)
	if !ok || lit.Kind != token.STRING {
		return 0
	}
	text, err := strconv.Unquote(lit.Value)
	if err != nil {
		return 0
	}
	n := 0
	for i := 0; i < len(text); i++ {
		if text[i] != '%' {
			continue
		}
		i++
		if i >= len(text) {
			break
		}
		if text[i] == '%' {
			continue
		}
		for i < len(text) && strings.IndexByte("+-# 0123456789.*[]", text[i]) >= 0 {
			i++
		}
		if i < len(text) && text[i] == 'w' {
			n++
		}
	}
	return n
}

// rootIdent returns the leftmost identifier of a selector chain, so that
// `build.GOPATH` and `build.Default.GOPATH` are both recognised as reaching
// into the same package.
func rootIdent(expr ast.Expr) string {
	for {
		switch e := expr.(type) {
		case *ast.Ident:
			return e.Name
		case *ast.SelectorExpr:
			expr = e.X
		default:
			return ""
		}
	}
}

// scanKeySortBlock counts the accumulate-then-sort sites in one statement
// list. See the KeySortLoops field for what counts and why it is read this
// narrowly.
func scanKeySortBlock(c *counts, list []ast.Stmt) {
	for i, stmt := range list {
		rng, ok := stmt.(*ast.RangeStmt)
		if !ok || rng.Tok != token.DEFINE || rng.Body == nil ||
			len(rng.Body.List) != 1 {
			continue
		}
		vars := loopVarNames(rng.Key, rng.Value)
		if len(vars) == 0 {
			continue
		}
		asn, ok := rng.Body.List[0].(*ast.AssignStmt)
		if !ok || asn.Tok != token.ASSIGN ||
			len(asn.Lhs) != 1 || len(asn.Rhs) != 1 {
			continue
		}
		dst, ok := asn.Lhs[0].(*ast.Ident)
		if !ok {
			continue
		}
		call, ok := asn.Rhs[0].(*ast.CallExpr)
		if !ok || len(call.Args) != 2 {
			continue
		}
		if fn, ok := call.Fun.(*ast.Ident); !ok || fn.Name != "append" {
			continue
		}
		if into, ok := call.Args[0].(*ast.Ident); !ok || into.Name != dst.Name {
			continue
		}
		app, ok := call.Args[1].(*ast.Ident)
		if !ok || !vars[app.Name] {
			continue
		}
		c.CollectLoops++
		if sortedLater(list[i+1:], dst.Name) {
			c.KeySortLoops++
		}
	}
}

// sortedLater reports whether one of the statements sorts the named slice in
// place. Both spellings this repository has used are recognised -- the
// `slices` package's in-place sorts and `sort`'s -- because the idiom is the
// pair, not the package it was written against.
func sortedLater(list []ast.Stmt, name string) bool {
	found := false
	for _, stmt := range list {
		ast.Inspect(stmt, func(n ast.Node) bool {
			call, ok := n.(*ast.CallExpr)
			if !ok || len(call.Args) == 0 {
				return true
			}
			sel, ok := call.Fun.(*ast.SelectorExpr)
			if !ok {
				return true
			}
			switch rootIdent(sel.X) {
			case "slices", "sort":
			default:
				return true
			}
			switch sel.Sel.Name {
			case "Sort", "SortFunc", "SortStableFunc", "Strings", "Ints",
				"Float64s", "Slice", "SliceStable":
			default:
				return true
			}
			if arg, ok := call.Args[0].(*ast.Ident); ok && arg.Name == name {
				found = true
			}
			return true
		})
		if found {
			return true
		}
	}
	return false
}

// isBlankDiscard reports whether a statement drops a returned value into `_`,
// the cheapest way to make a checker stop complaining without a comment. One
// node covers both spellings -- `_ = f()` and `x, _ := f()` differ only in
// AssignStmt.Tok -- and the right-hand side has to reach a call, which is what
// separates discarding something that was produced from assigning a value
// already in hand. A conversion is a call in the syntax and counts as one: the
// census reads shapes, not types.
func isBlankDiscard(a *ast.AssignStmt) bool {
	blank := false
	for _, lhs := range a.Lhs {
		if id, ok := lhs.(*ast.Ident); ok && id.Name == "_" {
			blank = true
			break
		}
	}
	if !blank {
		return false
	}
	for _, rhs := range a.Rhs {
		if containsCall(rhs) {
			return true
		}
	}
	return false
}

// containsCall reports whether an expression is a call or holds one.
func containsCall(expr ast.Expr) bool {
	found := false
	ast.Inspect(expr, func(n ast.Node) bool {
		if _, ok := n.(*ast.CallExpr); ok {
			found = true
		}
		return !found
	})
	return found
}

// sortedKeys keeps the JSON stable for diffing between runs.
func sortedKeys(m map[string]int) []string {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
}
