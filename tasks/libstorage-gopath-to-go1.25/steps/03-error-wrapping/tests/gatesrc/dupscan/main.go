// Command dupscan reports the declarations a migration to generics is meant to
// collapse: groups of functions whose bodies are the same program once the
// types they happen to be written for are erased.
//
// The question it answers is narrow on purpose. Two functions that are
// byte-identical are not necessarily a generics target -- a `Name() string`
// method repeated on six driver types is identical, and no type parameter can
// merge it, because a method cannot be generic over its own receiver. What
// generics do replace is the other shape: one body written out again for a
// second type, where the second copy differs from the first in nothing but the
// type it names. So a group counts only when its members agree on the body and
// disagree on a type somewhere other than the receiver.
//
// Normalisation is structural and type-checked rather than textual. Every
// expression that denotes a type is replaced by `T<n>`, numbered by the order
// in which distinct types first appear inside that declaration, so `[]string`
// and `map[string]any` collapse to the same token in the same position and an
// alias or a renamed import cannot separate two copies. Every local name --
// receiver, parameter, variable -- is replaced by a positional placeholder, so
// `v` and `tv` do not separate them either. Package-level and imported objects
// keep their names, because `strconv.ParseBool` and `strconv.ParseInt` are
// different programs, and struct fields keep theirs for the same reason.
//
// Unlike the other gate tools this is not a go/analysis checker: a clone group
// can straddle two packages that do not import each other, and go/analysis
// only carries facts along import edges.
package main

import (
	"flag"
	"fmt"
	"go/ast"
	"go/token"
	"go/types"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"

	"golang.org/x/tools/go/packages"
)

var (
	includeTests = flag.Bool("tests", false, "examine _test.go files as well")
	minGroup     = flag.Int("min-group", 3, "smallest group of clones to report")
	minNodes     = flag.Int("min-nodes", 12, "smallest declaration to consider, in AST nodes")
	root         = flag.String("root", "", "trim this prefix from reported paths")
)

// decl is one candidate declaration and everything the grouping needs.
type decl struct {
	pos       token.Position
	name      string
	pkg       string
	body      string // normalised signature + body, receiver type erased
	bodyTypes string // the canonical types the body itself names, joined
	nodes     int
}

func main() {
	flag.Parse()
	patterns := flag.Args()
	if len(patterns) == 0 {
		patterns = []string{"./..."}
	}

	cfg := &packages.Config{
		Mode: packages.NeedName | packages.NeedFiles | packages.NeedSyntax |
			packages.NeedTypes | packages.NeedTypesInfo | packages.NeedCompiledGoFiles,
		Tests: *includeTests,
	}
	pkgs, err := packages.Load(cfg, patterns...)
	if err != nil {
		fmt.Fprintf(os.Stderr, "dupscan: load: %v\n", err)
		os.Exit(2)
	}
	var loadErrs []string
	packages.Visit(pkgs, nil, func(p *packages.Package) {
		for _, e := range p.Errors {
			loadErrs = append(loadErrs, e.Error())
		}
	})
	if len(loadErrs) > 0 {
		// A tree that does not type-check is not a tree with no clones in it.
		// Say so on stderr and exit non-zero; the gate reads the exit code.
		sort.Strings(loadErrs)
		for _, e := range loadErrs[:min(len(loadErrs), 10)] {
			fmt.Fprintf(os.Stderr, "dupscan: %s\n", e)
		}
		os.Exit(3)
	}

	var decls []decl
	seen := map[string]bool{}
	for _, p := range pkgs {
		if seen[p.PkgPath] || p.TypesInfo == nil {
			continue
		}
		seen[p.PkgPath] = true
		for _, f := range p.Syntax {
			fname := p.Fset.Position(f.Pos()).Filename
			if !*includeTests && strings.HasSuffix(fname, "_test.go") {
				continue
			}
			for _, d := range f.Decls {
				fd, ok := d.(*ast.FuncDecl)
				if !ok || fd.Body == nil {
					continue
				}
				dd, ok := normalize(p, fd)
				if !ok || dd.nodes < *minNodes {
					continue
				}
				dd.pkg = p.PkgPath
				decls = append(decls, dd)
			}
		}
	}

	groups := map[string][]decl{}
	for _, d := range decls {
		groups[d.body] = append(groups[d.body], d)
	}
	var out []string
	for _, g := range groups {
		if len(g) < *minGroup {
			continue
		}
		// A group whose members name the same types in their bodies is not
		// a generics target: the difference between them is somewhere a type
		// parameter cannot reach.
		distinct := map[string]bool{}
		for _, d := range g {
			distinct[d.bodyTypes] = true
		}
		if len(distinct) < 2 {
			continue
		}
		sort.Slice(g, func(i, j int) bool {
			if g[i].pos.Filename != g[j].pos.Filename {
				return g[i].pos.Filename < g[j].pos.Filename
			}
			return g[i].pos.Line < g[j].pos.Line
		})
		for i, d := range g {
			others := make([]string, 0, len(g)-1)
			for j, o := range g {
				if i == j {
					continue
				}
				others = append(others, fmt.Sprintf("%s:%d %s",
					trim(o.pos.Filename, *root), o.pos.Line, o.name))
			}
			file := d.pos.Filename
			file = trim(file, *root)
			out = append(out, fmt.Sprintf(
				"%s:%d:%d: typed-clone: %s is one of %d declarations with the same body and a different type (%s)",
				file, d.pos.Line, d.pos.Column, d.name, len(g),
				strings.Join(others, ", ")))
		}
	}
	sort.Strings(out)
	for _, line := range out {
		fmt.Println(line)
	}
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

// normalize renders one declaration as the canonical string the grouping keys
// on, and separately the canonical types it names outside its receiver.
func normalize(p *packages.Package, fd *ast.FuncDecl) (decl, bool) {
	info := p.TypesInfo
	var buf strings.Builder
	nodes := 0

	// Canonical type numbering, in order of first appearance.
	typeIdx := map[string]int{}
	typeOf := func(e ast.Expr) (string, bool) {
		tv, ok := info.Types[e]
		if !ok || !tv.IsType() || tv.Type == nil {
			return "", false
		}
		return types.TypeString(tv.Type, func(pkg *types.Package) string {
			return pkg.Path()
		}), true
	}
	typeTok := func(canon string) string {
		if i, ok := typeIdx[canon]; ok {
			return "T" + strconv.Itoa(i)
		}
		i := len(typeIdx)
		typeIdx[canon] = i
		return "T" + strconv.Itoa(i)
	}

	// Local names, numbered in order of first appearance.
	localIdx := map[types.Object]int{}
	localTok := func(obj types.Object) string {
		if i, ok := localIdx[obj]; ok {
			return "v" + strconv.Itoa(i)
		}
		i := len(localIdx)
		localIdx[obj] = i
		return "v" + strconv.Itoa(i)
	}

	// The types the body itself names, which is where a difference has to
	// appear for a type parameter to be able to absorb it. Four kinds of
	// difference are deliberately not counted:
	//
	//   - the receiver's own type. `return (&errorHandler{m}).Handle` is the
	//     same method written on nine middleware types; a method cannot be
	//     generic over its receiver, so the nine are not one generic method
	//     waiting to be written.
	//   - the type of a composite literal that sets something. A type
	//     parameter cannot be constructed field by field, so
	//     `&types.VolumeCreateOpts{Opts: s}` and
	//     `&types.VolumeRemoveOpts{Opts: s}` are not one function either. An
	//     empty literal is excepted, because `&types.Volume{}` is `new(T)`
	//     and that is exactly what a generic reader writes.
	//   - a type named only in the signature. `API() types.APIClient` and
	//     `XCLI() types.StorageExecutorCLI` both `return &d.client`; the
	//     difference is a conversion the compiler performs, not a shape the
	//     body repeats.
	//   - a type handed to a generic call as a type argument. Six methods whose
	//     whole body is `return get[T](s, k)` for six different T are what this
	//     tool asks for rather than what it asks about: the body written out
	//     again for a second type is gone, and one generic declaration has taken
	//     its place. Go has no generic methods, so the six names have to stay; a
	//     difference already hoisted into a type argument is a difference a type
	//     parameter has already absorbed.
	bodyTypes := map[string]bool{}
	inRecv := false
	inBody := false
	recvType := ""
	if fd.Recv != nil && len(fd.Recv.List) == 1 {
		if tv, ok := info.Types[fd.Recv.List[0].Type]; ok && tv.Type != nil {
			t := tv.Type
			if ptr, ok := t.(*types.Pointer); ok {
				t = ptr.Elem()
			}
			recvType = types.TypeString(t, func(pkg *types.Package) string {
				return pkg.Path()
			})
		}
	}
	// Composite-literal type positions, collected up front so the walk can
	// recognise one when it reaches it.
	litType := map[ast.Expr]bool{}
	// Type-argument positions, collected the same way: the index of a generic
	// instantiation is a type this declaration hands to another one.
	typeArg := map[ast.Expr]bool{}
	ast.Inspect(fd, func(x ast.Node) bool {
		switch v := x.(type) {
		case *ast.CompositeLit:
			if v.Type != nil && len(v.Elts) > 0 {
				litType[v.Type] = true
			}
		case *ast.IndexExpr:
			if _, isType := typeOf(v.Index); isType {
				typeArg[v.Index] = true
			}
		case *ast.IndexListExpr:
			for _, ix := range v.Indices {
				if _, isType := typeOf(ix); isType {
					typeArg[ix] = true
				}
			}
		}
		return true
	})

	var walk func(n ast.Node)
	walk = func(n ast.Node) {
		ast.Inspect(n, func(x ast.Node) bool {
			if x == nil {
				buf.WriteString(")")
				return false
			}
			nodes++
			if e, ok := x.(ast.Expr); ok {
				if canon, isType := typeOf(e); isType {
					buf.WriteString("(" + typeTok(canon))
					if inBody && !inRecv && canon != recvType &&
						!litType[e] && !typeArg[e] {
						bodyTypes[canon] = true
					}
					return false
				}
			}
			switch v := x.(type) {
			case *ast.Ident:
				buf.WriteString("(" + identToken(info, v, localTok))
				return false
			case *ast.BasicLit:
				buf.WriteString("(lit:" + v.Kind.String() + ":" + v.Value)
				return false
			case *ast.SelectorExpr:
				// Rendered by the generic path: the X subtree, then the
				// selector, which keeps its name because a field or method
				// name is part of the program.
				buf.WriteString("(sel:" + v.Sel.Name)
				walkOnly(v.X, walk)
				buf.WriteString(")")
				return false
			case *ast.BinaryExpr:
				buf.WriteString("(bin:" + v.Op.String())
				return true
			case *ast.UnaryExpr:
				buf.WriteString("(un:" + v.Op.String())
				return true
			case *ast.AssignStmt:
				buf.WriteString("(asn:" + v.Tok.String())
				return true
			}
			buf.WriteString("(" + nodeKind(x))
			return true
		})
	}
	_ = walk

	buf.WriteString("(func")
	if fd.Recv != nil {
		inRecv = true
		walk(fd.Recv)
		inRecv = false
	}
	walk(fd.Type)
	inBody = true
	walk(fd.Body)
	inBody = false
	buf.WriteString(")")

	keys := make([]string, 0, len(bodyTypes))
	for k := range bodyTypes {
		keys = append(keys, k)
	}
	sort.Strings(keys)

	return decl{
		pos:       p.Fset.Position(fd.Pos()),
		name:      fd.Name.Name,
		body:      buf.String(),
		bodyTypes: strings.Join(keys, "|"),
		nodes:     nodes,
	}, true
}

// trim renders a path relative to the tree root when one was given.
func trim(path, root string) string {
	if root == "" {
		return path
	}
	if rel, err := filepath.Rel(root, path); err == nil {
		return rel
	}
	return path
}

func walkOnly(n ast.Node, walk func(ast.Node)) {
	if n != nil {
		walk(n)
	}
}

// identToken renders one identifier. A local keeps only its position in the
// declaration; anything declared outside keeps its name, qualified by the
// package it came from, because two different functions are two different
// programs however similar their shapes.
func identToken(info *types.Info, id *ast.Ident, localTok func(types.Object) string) string {
	obj := info.Uses[id]
	if obj == nil {
		obj = info.Defs[id]
	}
	if obj == nil {
		return "id:" + id.Name
	}
	switch o := obj.(type) {
	case *types.Var:
		if o.IsField() {
			return "fld:" + o.Name()
		}
		if o.Parent() != nil && o.Pkg() != nil && o.Parent() == o.Pkg().Scope() {
			return "gvar:" + qual(o)
		}
		return localTok(o)
	case *types.Const:
		if o.Pkg() == nil {
			return "bconst:" + o.Name()
		}
		return "const:" + qual(o)
	case *types.Func:
		return "fn:" + qual(o)
	case *types.TypeName:
		return "tn:" + qual(o)
	case *types.Builtin:
		return "builtin:" + o.Name()
	case *types.Nil:
		return "nil"
	case *types.PkgName:
		return "pkg:" + o.Imported().Path()
	case *types.Label:
		return "label"
	}
	return "id:" + id.Name
}

func qual(o types.Object) string {
	if o.Pkg() == nil {
		return o.Name()
	}
	return o.Pkg().Path() + "." + o.Name()
}

func nodeKind(n ast.Node) string {
	s := fmt.Sprintf("%T", n)
	if i := strings.LastIndex(s, "."); i >= 0 {
		s = s[i+1:]
	}
	return s
}
