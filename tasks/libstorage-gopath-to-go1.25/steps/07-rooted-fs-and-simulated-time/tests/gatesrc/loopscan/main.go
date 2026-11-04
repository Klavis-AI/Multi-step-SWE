// Command loopscan reports the closures that reach past a value they were
// handed for the enclosing loop's copy of the same thing.
//
// The shape is narrow and it is the shape a loop-variable migration leaves
// behind. A function literal written inside a loop is given the value it is to
// work on as a parameter -- because whoever runs it later knows which one that
// is -- and then, somewhere in its body, ignores the parameter and reads the
// loop's own variable instead. Through Go 1.21 the loop had one variable for
// every iteration, so such a read saw whatever the loop had moved on to by the
// time the literal ran; from Go 1.22 it sees the iteration's own value, which
// papers the defect over without removing it. Either way the literal is not
// reading what it was handed, and only source can show it: on a tree where the
// loop and the runner agree, both readings produce the same answer.
//
// "For the same thing" is decided by type rather than by name, which is the
// whole reason this is a typed tool and not an AST census. A literal counts
// only when it declares a parameter of the loop variable's own type: that is
// the parameter the value arrives in, whatever it is called. A handler that
// takes `(http.ResponseWriter, *http.Request)` and reads the route it is being
// registered for is not this shape -- nothing handed it the route -- and is
// correct at Go 1.22, so it is not reported. Renaming the parameter does not
// move the count, and neither does renaming the loop variable: the references
// are resolved by the type checker, so a name shadowed in between is not a
// reference at all.
//
// A copy the loop body makes of its own variable is followed: `s := service`
// written inside the loop, and then a literal that was handed the service
// reading `s`. That copy was how a closure got the iteration's own value
// through Go 1.21 and it is the workaround this milestone retires; reading it
// is still not reading the parameter. Only a short declaration whose whole
// right-hand side is the variable counts, and a copy of a copy counts with it.
//
// Each reference is one finding, reported once even when it sits inside a
// literal nested in the literal that was handed the value.
package main

import (
	"flag"
	"fmt"
	"go/ast"
	"go/token"
	"go/types"
	"os"
	"sort"
	"strings"

	"golang.org/x/tools/go/packages"
)

var (
	includeTests = flag.Bool("tests", false, "examine _test.go files as well")
	root         = flag.String("root", "", "trim this prefix from reported paths")
)

func main() {
	flag.Parse()
	patterns := flag.Args()
	if len(patterns) == 0 {
		patterns = []string{"./..."}
	}

	cfg := &packages.Config{
		Mode: packages.NeedName | packages.NeedFiles | packages.NeedSyntax |
			packages.NeedTypes | packages.NeedTypesInfo |
			packages.NeedCompiledGoFiles,
		Tests: *includeTests,
	}
	pkgs, err := packages.Load(cfg, patterns...)
	if err != nil {
		fmt.Fprintf(os.Stderr, "loopscan: load: %v\n", err)
		os.Exit(2)
	}
	var loadErrs []string
	packages.Visit(pkgs, nil, func(p *packages.Package) {
		for _, e := range p.Errors {
			loadErrs = append(loadErrs, e.Error())
		}
	})
	if len(loadErrs) > 0 {
		// A tree that does not type-check is not a tree with no captures in
		// it. Say so on stderr and exit non-zero; the gate reads the code.
		sort.Strings(loadErrs)
		for _, e := range loadErrs[:min(len(loadErrs), 10)] {
			fmt.Fprintf(os.Stderr, "loopscan: %s\n", e)
		}
		os.Exit(3)
	}

	var out []string
	seenPkg := map[string]bool{}
	for _, p := range pkgs {
		if seenPkg[p.PkgPath] || p.TypesInfo == nil {
			continue
		}
		seenPkg[p.PkgPath] = true
		for _, f := range p.Syntax {
			fname := p.Fset.Position(f.Pos()).Filename
			if !*includeTests && strings.HasSuffix(fname, "_test.go") {
				continue
			}
			out = append(out, scanFile(p, f)...)
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

// scanFile reports every capture in one file, one line per reference.
func scanFile(p *packages.Package, f *ast.File) []string {
	// Reported once per reference, however many literals enclose it.
	seen := map[token.Pos]string{}
	ast.Inspect(f, func(n ast.Node) bool {
		vars := loopVars(p, n)
		if len(vars) == 0 {
			return true
		}
		for _, hit := range scanLoop(p, vars, loopBody(n)) {
			if _, ok := seen[hit.pos]; !ok {
				seen[hit.pos] = hit.line
			}
		}
		return true
	})
	lines := make([]string, 0, len(seen))
	for _, line := range seen {
		lines = append(lines, line)
	}
	return lines
}

// loopVars is the set of variables a loop statement declared for itself.
// Only the `:=` forms are here: a loop that assigns to variables declared
// somewhere else never had per-iteration semantics to begin with, so a
// literal that copies one of those is still doing necessary work.
func loopVars(p *packages.Package, n ast.Node) []*types.Var {
	var idents []*ast.Ident
	switch stmt := n.(type) {
	case *ast.RangeStmt:
		if stmt.Tok != token.DEFINE {
			return nil
		}
		for _, e := range []ast.Expr{stmt.Key, stmt.Value} {
			if id, ok := e.(*ast.Ident); ok {
				idents = append(idents, id)
			}
		}
	case *ast.ForStmt:
		assign, ok := stmt.Init.(*ast.AssignStmt)
		if !ok || assign.Tok != token.DEFINE {
			return nil
		}
		for _, e := range assign.Lhs {
			if id, ok := e.(*ast.Ident); ok {
				idents = append(idents, id)
			}
		}
	default:
		return nil
	}
	var out []*types.Var
	for _, id := range idents {
		if id.Name == "_" {
			continue
		}
		obj, ok := p.TypesInfo.Defs[id].(*types.Var)
		if ok && obj != nil && obj.Type() != nil {
			out = append(out, obj)
		}
	}
	return out
}

func loopBody(n ast.Node) *ast.BlockStmt {
	switch stmt := n.(type) {
	case *ast.RangeStmt:
		return stmt.Body
	case *ast.ForStmt:
		return stmt.Body
	}
	return nil
}

type hit struct {
	pos  token.Pos
	line string
}

// scanLoop reports the references one loop's body makes to the loop's own
// variables from inside a literal that was handed a value of that variable's
// type.
func scanLoop(
	p *packages.Package, vars []*types.Var, body *ast.BlockStmt) []hit {

	if body == nil {
		return nil
	}
	var out []hit
	copies := loopVarCopies(p, vars, body)
	ast.Inspect(body, func(n ast.Node) bool {
		lit, ok := n.(*ast.FuncLit)
		if !ok {
			return true
		}
		for _, v := range vars {
			param := handedParam(p, lit, v.Type())
			if param == nil {
				continue
			}
			ast.Inspect(lit.Body, func(m ast.Node) bool {
				id, ok := m.(*ast.Ident)
				if !ok {
					return true
				}
				read, _ := p.TypesInfo.Uses[id].(*types.Var)
				if read == nil || (read != v && copies[read] != v) {
					return true
				}
				what := "the enclosing loop's " + v.Name()
				if read != v {
					what = fmt.Sprintf(
						"%s, the loop's %s copied inside the loop",
						read.Name(), v.Name())
				}
				pos := p.Fset.Position(id.Pos())
				out = append(out, hit{
					pos: id.Pos(),
					line: fmt.Sprintf(
						"%s:%d:%d: loop-capture: %s reads %s, and was "+
							"handed one as %s %s",
						trim(pos.Filename, *root), pos.Line, pos.Column,
						describe(p, lit), what, param.Name(),
						types.TypeString(param.Type(), qualifier)),
				})
				return true
			})
		}
		return true
	})
	return out
}

// loopVarCopies maps every variable the loop body declares as a copy of one
// of the loop's own to the loop variable it came from: `service := service`,
// `s := service`, and `t := s` after either. Only a short declaration whose
// whole right-hand side is the identifier counts -- a value built out of the
// loop variable by an expression is a different value, and an assignment to a
// variable declared outside the loop is not this idiom. Two passes, so that a
// copy of a copy is found whichever order the two are written in; a longer
// chain than that is not a workaround anyone writes.
func loopVarCopies(
	p *packages.Package,
	vars []*types.Var,
	body *ast.BlockStmt) map[*types.Var]*types.Var {

	out := map[*types.Var]*types.Var{}
	if body == nil {
		return out
	}
	from := func(v *types.Var) *types.Var {
		for _, loop := range vars {
			if v == loop {
				return loop
			}
		}
		return out[v]
	}
	for pass := 0; pass < 2; pass++ {
		ast.Inspect(body, func(n ast.Node) bool {
			a, ok := n.(*ast.AssignStmt)
			if !ok || a.Tok != token.DEFINE ||
				len(a.Lhs) != 1 || len(a.Rhs) != 1 {
				return true
			}
			rhs, ok := a.Rhs[0].(*ast.Ident)
			if !ok {
				return true
			}
			read, _ := p.TypesInfo.Uses[rhs].(*types.Var)
			if read == nil {
				return true
			}
			loop := from(read)
			if loop == nil {
				return true
			}
			lhs, ok := a.Lhs[0].(*ast.Ident)
			if !ok || lhs.Name == "_" {
				return true
			}
			made, _ := p.TypesInfo.Defs[lhs].(*types.Var)
			if made == nil || made == loop {
				return true
			}
			out[made] = loop
			return true
		})
	}
	return out
}

// handedParam is the literal's own parameter that the loop variable's value
// arrives in: the first one whose type is that variable's type. A parameter of
// some other type is not a stand-in for this variable and says nothing about
// how the literal ought to read it.
func handedParam(
	p *packages.Package, lit *ast.FuncLit, want types.Type) *types.Var {

	if lit.Type == nil || lit.Type.Params == nil {
		return nil
	}
	for _, field := range lit.Type.Params.List {
		for _, name := range field.Names {
			if name.Name == "_" {
				continue
			}
			obj, ok := p.TypesInfo.Defs[name].(*types.Var)
			if !ok || obj == nil || obj.Type() == nil {
				continue
			}
			if types.Identical(obj.Type(), want) {
				return obj
			}
		}
	}
	return nil
}

// describe names a literal by the signature a reader would recognise it by.
func describe(p *packages.Package, lit *ast.FuncLit) string {
	tv, ok := p.TypesInfo.Types[lit]
	if !ok || tv.Type == nil {
		return "a function literal"
	}
	sig := types.TypeString(tv.Type, qualifier)
	return "the literal " + sig
}

func qualifier(pkg *types.Package) string { return pkg.Name() }

func trim(path, prefix string) string {
	if prefix == "" {
		return path
	}
	prefix = strings.TrimSuffix(prefix, "/") + "/"
	return strings.TrimPrefix(path, prefix)
}
