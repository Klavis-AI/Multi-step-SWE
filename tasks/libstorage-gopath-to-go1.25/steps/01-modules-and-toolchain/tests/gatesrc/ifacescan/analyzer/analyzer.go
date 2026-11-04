// Package analyzer reports the untyped containers a migration is supposed to
// remove: struct fields, function parameters and results, and named type
// declarations whose type is, or is a container of, the empty interface.
//
// Only the packages the checker is pointed at are examined, and only the type
// written at that position is examined: a field of a named type declared
// elsewhere in the package is not reported twice, because the declaration
// itself is reported where it is written.
package analyzer

import (
	"go/ast"
	"go/types"
	"strings"

	"golang.org/x/tools/go/analysis"
	"golang.org/x/tools/go/analysis/passes/inspect"
	"golang.org/x/tools/go/ast/inspector"
)

// Analyzer is the go/analysis pass the gate runs.
var Analyzer = &analysis.Analyzer{
	Name:     "ifacescan",
	Doc:      "report empty-interface fields, parameters, results and type declarations",
	Requires: []*analysis.Analyzer{inspect.Analyzer},
	Run:      run,
}

// IncludeTests controls whether _test.go files are examined. The gate sets it
// from a flag so the same binary can answer both questions.
var IncludeTests bool

func init() {
	Analyzer.Flags.BoolVar(&IncludeTests, "tests", true,
		"examine _test.go files as well")
}

// isEmptyInterface reports whether t is `interface{}` / `any` itself.
//
// A type parameter whose constraint is `any` is not an empty interface: it is
// a type parameter, and generic code is the destination of this migration
// rather than something it is trying to remove.
func isEmptyInterface(t types.Type) bool {
	if t == nil {
		return false
	}
	if _, ok := types.Unalias(t).(*types.TypeParam); ok {
		return false
	}
	iface, ok := types.Unalias(t).(*types.Interface)
	return ok && iface.Empty()
}

// containsEmptyInterface walks the composite structure of t -- pointers,
// slices, arrays, maps, channels, function signatures and anonymous structs --
// looking for the empty interface. Named types are not expanded: their own
// declaration is the place the finding belongs.
func containsEmptyInterface(t types.Type, depth int) bool {
	if t == nil || depth > 24 {
		return false
	}
	if isEmptyInterface(t) {
		return true
	}
	switch u := types.Unalias(t).(type) {
	case *types.Pointer:
		return containsEmptyInterface(u.Elem(), depth+1)
	case *types.Slice:
		return containsEmptyInterface(u.Elem(), depth+1)
	case *types.Array:
		return containsEmptyInterface(u.Elem(), depth+1)
	case *types.Chan:
		return containsEmptyInterface(u.Elem(), depth+1)
	case *types.Map:
		return containsEmptyInterface(u.Key(), depth+1) ||
			containsEmptyInterface(u.Elem(), depth+1)
	case *types.Struct:
		for i := 0; i < u.NumFields(); i++ {
			if containsEmptyInterface(u.Field(i).Type(), depth+1) {
				return true
			}
		}
	case *types.Signature:
		return tupleHasEmpty(u.Params(), depth) || tupleHasEmpty(u.Results(), depth)
	}
	return false
}

func tupleHasEmpty(tp *types.Tuple, depth int) bool {
	if tp == nil {
		return false
	}
	for i := 0; i < tp.Len(); i++ {
		if containsEmptyInterface(tp.At(i).Type(), depth+1) {
			return true
		}
	}
	return false
}

func run(pass *analysis.Pass) (interface{}, error) {
	insp := pass.ResultOf[inspect.Analyzer].(*inspector.Inspector)

	skip := func(n ast.Node) bool {
		if IncludeTests {
			return false
		}
		name := pass.Fset.Position(n.Pos()).Filename
		return strings.HasSuffix(name, "_test.go")
	}

	report := func(n ast.Node, kind, name string, t types.Type) {
		if !containsEmptyInterface(t, 0) || skip(n) {
			return
		}
		label := name
		if label == "" {
			label = "<unnamed>"
		}
		pass.Reportf(n.Pos(), "%s %s has untyped container type %s",
			kind, label, types.TypeString(t, types.RelativeTo(pass.Pkg)))
	}

	// WithStack rather than Preorder: a signature is reported once, at its
	// FuncType, and the stack supplies the name of whatever declares it.
	insp.WithStack([]ast.Node{
		(*ast.StructType)(nil),
		(*ast.FuncType)(nil),
		(*ast.TypeSpec)(nil),
	}, func(n ast.Node, push bool, stack []ast.Node) bool {
		if !push {
			return false
		}
		switch node := n.(type) {
		case *ast.StructType:
			for _, f := range node.Fields.List {
				t := pass.TypesInfo.TypeOf(f.Type)
				if len(f.Names) == 0 {
					report(f, "embedded field", "", t)
					continue
				}
				for _, id := range f.Names {
					report(f, "field", id.Name, t)
				}
			}

		case *ast.FuncType:
			reportSignature(pass, report, enclosingName(stack), node)

		case *ast.TypeSpec:
			// A named func or struct type is described by the nodes inside
			// it; only a container spelled out here is reported at the name.
			if _, ok := node.Type.(*ast.FuncType); ok {
				return true
			}
			if _, ok := node.Type.(*ast.StructType); ok {
				return true
			}
			report(node, "type", node.Name.Name,
				pass.TypesInfo.TypeOf(node.Type))
		}
		return true
	})

	return nil, nil
}

// enclosingName walks outward for something worth naming in a diagnostic.
func enclosingName(stack []ast.Node) string {
	for i := len(stack) - 1; i >= 0; i-- {
		switch node := stack[i].(type) {
		case *ast.FuncDecl:
			return node.Name.Name
		case *ast.TypeSpec:
			return node.Name.Name
		}
	}
	return "func literal"
}

func reportSignature(
	pass *analysis.Pass,
	report func(ast.Node, string, string, types.Type),
	owner string,
	ft *ast.FuncType) {

	each := func(list *ast.FieldList, kind string) {
		if list == nil {
			return
		}
		for _, f := range list.List {
			t := pass.TypesInfo.TypeOf(f.Type)
			label := owner
			if len(f.Names) > 0 {
				names := make([]string, 0, len(f.Names))
				for _, id := range f.Names {
					names = append(names, id.Name)
				}
				label = owner + "(" + strings.Join(names, ", ") + ")"
			}
			report(f, kind, label, t)
		}
	}
	each(ft.Params, "parameter of")
	each(ft.Results, "result of")
}
