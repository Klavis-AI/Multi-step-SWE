// Package analyzer reports the ways a Go program can ask a question about an
// error without going through `errors.Is` or `errors.As`, and the way it can
// mention a cause without keeping it.
//
// Every category here has the same defect in it: the answer is read off the
// error value in front of the caller rather than off the chain that value
// stands at the head of. A comparison, a text match, a type switch and a type
// assertion all stop at the first link, so each of them turns from right to
// wrong the moment anything between the producer and the consumer adds context
// -- which is what wrapping is for. A `%v` that renders an error into a new
// message is the same defect from the producing end: the text survives and the
// value does not.
//
// Recognising an error by its text is reported however it is spelled. `==` on
// the result of `Error()` is the form this repository happens to contain, but
// `strings.Contains` on that result answers the same question in a strictly
// more fragile way -- it is true of any error whose message merely contains
// the sentinel's -- and is reported the same way, as is a match against text
// that `fmt` produced from an error rather than the error's own method.
//
// The pass is type-checked rather than syntactic, so an aliased import, a
// renamed type or a dot-import cannot hide a site from it, and a comparison
// between two things that merely *look* like errors is not counted.
//
// Only the packages the checker is pointed at are examined.
package analyzer

import (
	"go/ast"
	"go/constant"
	"go/token"
	"go/types"
	"strings"

	"golang.org/x/tools/go/analysis"
	"golang.org/x/tools/go/analysis/passes/inspect"
	"golang.org/x/tools/go/ast/inspector"
)

// Analyzer is the go/analysis pass the gate runs.
var Analyzer = &analysis.Analyzer{
	Name:     "errscan",
	Doc:      "report error comparisons, error type switches and dropped causes",
	Requires: []*analysis.Analyzer{inspect.Analyzer},
	Run:      run,
}

// The categories this pass can report. A gate names the ones it grades, so
// that one binary answers several separate questions and each of them can sit
// in the family it belongs to.
//
// `dropped-cause` covers both shapes the defect takes, because they are the
// same destruction written from two directions. One is a cause rendered at
// `fmt.Errorf` under a verb that formats it instead of `%w`. The other is a
// message carried into a new standard-library error without its value --
// `errors.New(fmt.Sprintf("...: %v", err))` or the same thing built by
// concatenating `err.Error()` -- which produces a byte-identical message while
// leaving nothing behind for `errors.Is` to walk to, and which would otherwise
// read clean here and on a count of `%w` verbs alike.
const (
	catIdentityCmp  = "identity-cmp"
	catStringCmp    = "string-cmp"
	catTypeSwitch   = "concrete-type-switch"
	catTypeAssert   = "concrete-type-assert"
	catDroppedCause = "dropped-cause"
)

var (
	// IncludeTests controls whether _test.go files are examined.
	IncludeTests bool
	// Only is a comma-separated list of categories to report; empty means all.
	Only string
)

func init() {
	Analyzer.Flags.BoolVar(&IncludeTests, "tests", true,
		"examine _test.go files as well")
	Analyzer.Flags.StringVar(&Only, "only", "",
		"comma-separated categories to report (default: all)")
}

// errorIface is the `error` interface from the universe scope.
func errorIface() *types.Interface {
	return types.Universe.Lookup("error").Type().Underlying().(*types.Interface)
}

// isError reports whether t is the `error` interface itself or anything that
// satisfies it. A nil type, and the untyped nil, are neither.
func isError(t types.Type) bool {
	if t == nil {
		return false
	}
	if b, ok := types.Unalias(t).(*types.Basic); ok && b.Kind() == types.UntypedNil {
		return false
	}
	return types.Implements(t, errorIface())
}

// isConcrete reports whether t names something with a single dynamic type --
// a struct, a pointer, or any other non-interface. Asserting an error to an
// *interface* is a capability question ("does this error carry fields?") and
// is answered the same way before and after a migration; asserting it to a
// concrete type is a question about identity, and that is the one `errors.As`
// exists to answer.
func isConcrete(t types.Type) bool {
	if t == nil {
		return false
	}
	_, iface := types.Unalias(t).Underlying().(*types.Interface)
	return !iface
}

// isErrorMethodCall reports whether e is `x.Error()` on something that is an
// error -- the call whose result is the only thing a string comparison can
// have been looking at.
func isErrorMethodCall(info *types.Info, e ast.Expr) bool {
	call, ok := astUnparen(e).(*ast.CallExpr)
	if !ok || len(call.Args) != 0 {
		return false
	}
	sel, ok := astUnparen(call.Fun).(*ast.SelectorExpr)
	if !ok || sel.Sel.Name != "Error" {
		return false
	}
	return isError(info.TypeOf(sel.X))
}

// textMatchers are the standard-library calls that answer a question about a
// string by looking at what the string contains. Applied to an error's message
// they are the same construct as `==` on that message, and a worse one: `==`
// at least fails loudly when the wording changes, whereas a substring test goes
// on quietly answering yes for as long as the sentinel's words survive
// anywhere in the message -- including inside somebody else's.
//
// Only predicates are listed. A call that merely rearranges a message for
// display, such as `strings.Replace` or `strings.ToLower`, is not asking a
// question about which error it has, and text that has been through one of
// those on the way into a predicate is still caught, because the argument is
// searched rather than just matched.
var textMatchers = map[string]bool{
	"strings.Contains":                     true,
	"strings.ContainsAny":                  true,
	"strings.ContainsRune":                 true,
	"strings.HasPrefix":                    true,
	"strings.HasSuffix":                    true,
	"strings.EqualFold":                    true,
	"strings.Index":                        true,
	"strings.IndexAny":                     true,
	"strings.LastIndex":                    true,
	"strings.Count":                        true,
	"strings.Compare":                      true,
	"regexp.Match":                         true,
	"regexp.MatchString":                   true,
	"*regexp.Regexp.Match":                 true,
	"*regexp.Regexp.MatchString":           true,
	"*regexp.Regexp.FindString":            true,
	"*regexp.Regexp.FindStringIndex":       true,
	"*regexp.Regexp.FindStringSubmatch":    true,
	"*regexp.Regexp.FindAllString":         true,
	"*regexp.Regexp.FindAllStringSubmatch": true,
}

// calleeName is the fully qualified name of what a call resolves to, so that
// an aliased or dot-imported package cannot hide a site. Methods are named by
// their receiver type.
func calleeName(info *types.Info, call *ast.CallExpr) string {
	sel, ok := astUnparen(call.Fun).(*ast.SelectorExpr)
	if !ok {
		return ""
	}
	fn, ok := info.Uses[sel.Sel].(*types.Func)
	if !ok {
		return ""
	}
	sig, ok := fn.Type().(*types.Signature)
	if ok && sig.Recv() != nil {
		return types.TypeString(sig.Recv().Type(), nil) + "." + fn.Name()
	}
	if fn.Pkg() == nil {
		return ""
	}
	return fn.Pkg().Path() + "." + fn.Name()
}

// isErrorRendering reports whether e is a `fmt` call that turns a value into a
// string and was handed an error to turn. Its result is an error's message by
// another route, and a predicate applied to it is the same construct as a
// predicate applied to `Error()`.
func isErrorRendering(info *types.Info, e ast.Expr) bool {
	call, ok := astUnparen(e).(*ast.CallExpr)
	if !ok {
		return false
	}
	switch calleeName(info, call) {
	case "fmt.Sprint", "fmt.Sprintf", "fmt.Sprintln":
	default:
		return false
	}
	for _, arg := range call.Args {
		if isError(info.TypeOf(arg)) {
			return true
		}
	}
	return false
}

// carriesErrorText reports whether anything inside e is an error's message.
// The whole argument is searched, not just its outermost call, so that wrapping
// the message in a `strings.ToLower` or concatenating it with something else
// on the way into a predicate does not hide the site.
func carriesErrorText(info *types.Info, e ast.Expr) bool {
	found := false
	ast.Inspect(e, func(n ast.Node) bool {
		if found {
			return false
		}
		expr, ok := n.(ast.Expr)
		if !ok {
			return true
		}
		if isErrorMethodCall(info, expr) || isErrorRendering(info, expr) {
			found = true
			return false
		}
		return true
	})
	return found
}

func astUnparen(e ast.Expr) ast.Expr {
	for {
		p, ok := e.(*ast.ParenExpr)
		if !ok {
			return e
		}
		e = p.X
	}
}

// inIsMethod reports whether the stack sits inside a method named `Is` whose
// signature is `func(error) bool`. That method is the documented way for a
// type to take part in `errors.Is`, and the comparison it is required to make
// is a comparison between two errors. Reporting it would be reporting the
// migration's own destination.
func inIsMethod(info *types.Info, stack []ast.Node) bool {
	for i := len(stack) - 1; i >= 0; i-- {
		fn, ok := stack[i].(*ast.FuncDecl)
		if !ok {
			continue
		}
		if fn.Recv == nil || fn.Name.Name != "Is" {
			return false
		}
		sig, ok := info.Defs[fn.Name].(*types.Func)
		if !ok {
			return false
		}
		s := sig.Type().(*types.Signature)
		if s.Params().Len() != 1 || s.Results().Len() != 1 {
			return false
		}
		if !types.Identical(s.Params().At(0).Type(),
			types.Universe.Lookup("error").Type()) {
			return false
		}
		b, ok := types.Unalias(s.Results().At(0).Type()).(*types.Basic)
		return ok && b.Kind() == types.Bool
	}
	return false
}

// verbs maps each operand position of a format string to the verb that
// consumes it, so that "is this error rendered by %w" can be asked of the
// argument rather than of the string as a whole. Positions the scanner cannot
// account for -- explicit argument indexes, `*` widths -- yield an empty
// string, which is read as "not %w" by the caller and so is reported. That
// direction is deliberate: the pass would rather name a site a human has to
// look at than stay quiet about one.
func verbs(format string) []byte {
	out := []byte{}
	for i := 0; i < len(format); i++ {
		if format[i] != '%' {
			continue
		}
		i++
		if i >= len(format) {
			break
		}
		if format[i] == '%' {
			continue
		}
		// flags, width and precision
		for i < len(format) && strings.IndexByte("+-# 0123456789.*[]", format[i]) >= 0 {
			i++
		}
		if i >= len(format) {
			break
		}
		out = append(out, format[i])
	}
	return out
}

// isStdErrorCtor reports whether call is one of the two standard-library ways
// to build an error from a message: `errors.New` and `fmt.Errorf`.
//
// The list is deliberately these two and no more. Both of them document a way
// to keep a cause -- `fmt.Errorf` has `%w`, and a caller who reaches for
// `errors.New` had `fmt.Errorf` available and did not take it -- so rendering
// an error's message into one of them and dropping the value is an unambiguous
// choice of the text over the value. A first-party constructor handed only a
// message is not: it may be a legitimate serialisation boundary, where turning
// an arbitrary error into text is the whole point. This repository's finished
// tree has exactly such a boundary, where an error is rendered into the wire
// envelope's detail type, and a version of this rule that fired on any call
// returning an error reported it twice.
func isStdErrorCtor(info *types.Info, call *ast.CallExpr) bool {
	switch calleeName(info, call) {
	case "errors.New", "fmt.Errorf":
		return true
	}
	return false
}

// anyArgIsError reports whether the call was handed an error value anywhere in
// its arguments. A call that was is answered by the `%w` rule below instead:
// the two rules are about the same defect from opposite ends, and this is what
// keeps both of them from firing on one call.
func anyArgIsError(info *types.Info, call *ast.CallExpr) bool {
	for _, arg := range call.Args {
		if isError(info.TypeOf(arg)) {
			return true
		}
	}
	return false
}

// isErrorf reports whether call is `fmt.Errorf`, however the package was
// imported, and returns the index of its first operand.
func isErrorf(info *types.Info, call *ast.CallExpr) bool {
	sel, ok := astUnparen(call.Fun).(*ast.SelectorExpr)
	if !ok || sel.Sel.Name != "Errorf" {
		return false
	}
	fn, ok := info.Uses[sel.Sel].(*types.Func)
	if !ok || fn.Pkg() == nil {
		return false
	}
	return fn.Pkg().Path() == "fmt"
}

func run(pass *analysis.Pass) (interface{}, error) {
	insp := pass.ResultOf[inspect.Analyzer].(*inspector.Inspector)
	info := pass.TypesInfo

	want := map[string]bool{}
	for _, c := range strings.Split(Only, ",") {
		if c = strings.TrimSpace(c); c != "" {
			want[c] = true
		}
	}

	report := func(n ast.Node, cat, format string, args ...interface{}) {
		if len(want) > 0 && !want[cat] {
			return
		}
		if !IncludeTests &&
			strings.HasSuffix(pass.Fset.Position(n.Pos()).Filename, "_test.go") {
			return
		}
		pass.Reportf(n.Pos(), cat+": "+format, args...)
	}

	insp.WithStack([]ast.Node{
		(*ast.BinaryExpr)(nil),
		(*ast.TypeSwitchStmt)(nil),
		(*ast.TypeAssertExpr)(nil),
		(*ast.CallExpr)(nil),
	}, func(n ast.Node, push bool, stack []ast.Node) bool {
		if !push {
			return true
		}
		switch node := n.(type) {

		case *ast.BinaryExpr:
			if node.Op != token.EQL && node.Op != token.NEQ {
				return true
			}
			if isErrorMethodCall(info, node.X) || isErrorMethodCall(info, node.Y) {
				report(node, catStringCmp,
					"an error is compared by the text of its Error method")
				return true
			}
			if isError(info.TypeOf(node.X)) && isError(info.TypeOf(node.Y)) &&
				!inIsMethod(info, stack) {
				report(node, catIdentityCmp,
					"two errors are compared with %s", node.Op)
			}

		case *ast.TypeSwitchStmt:
			subject := typeSwitchSubject(node)
			if subject == nil || !isError(info.TypeOf(subject)) {
				return true
			}
			for _, stmt := range node.Body.List {
				cc, ok := stmt.(*ast.CaseClause)
				if !ok {
					continue
				}
				for _, expr := range cc.List {
					if isConcrete(info.TypeOf(expr)) {
						report(node, catTypeSwitch,
							"an error is classified by a switch on its "+
								"dynamic type")
						return true
					}
				}
			}

		case *ast.TypeAssertExpr:
			if node.Type == nil { // the x.(type) inside a type switch
				return true
			}
			if isError(info.TypeOf(node.X)) && isConcrete(info.TypeOf(node.Type)) {
				report(node, catTypeAssert,
					"an error is asserted to the concrete type %s",
					types.TypeString(info.TypeOf(node.Type),
						types.RelativeTo(pass.Pkg)))
			}

		case *ast.CallExpr:
			if textMatchers[calleeName(info, node)] {
				for _, arg := range node.Args {
					if carriesErrorText(info, arg) {
						report(node, catStringCmp,
							"an error is recognised by matching the "+
								"text of its Error method")
						return true
					}
				}
			}
			// The other way to drop a cause: keep the message and build a
			// fresh error out of it. `errors.New(fmt.Sprintf("...: %v", err))`
			// and `errors.New("...: " + err.Error())` destroy exactly what
			// `%w` would have kept, and leave no verb behind to say so.
			if isStdErrorCtor(info, node) && !anyArgIsError(info, node) {
				for _, arg := range node.Args {
					if carriesErrorText(info, arg) {
						report(arg, catDroppedCause,
							"an error's message is carried into a "+
								"new error but its value is not")
						break
					}
				}
			}
			if !isErrorf(info, node) || len(node.Args) == 0 {
				return true
			}
			tv, ok := info.Types[node.Args[0]]
			if !ok || tv.Value == nil || tv.Value.Kind() != constant.String {
				return true
			}
			vs := verbs(constant.StringVal(tv.Value))
			for i, arg := range node.Args[1:] {
				if !isError(info.TypeOf(arg)) {
					continue
				}
				if i < len(vs) && vs[i] == 'w' {
					continue
				}
				report(arg, catDroppedCause,
					"an error is rendered into a new message without %%w")
			}
		}
		return true
	})

	return nil, nil
}

// typeSwitchSubject returns the expression a type switch is switching on,
// whether it was written as `switch x.(type)` or `switch v := x.(type)`.
func typeSwitchSubject(node *ast.TypeSwitchStmt) ast.Expr {
	switch assign := node.Assign.(type) {
	case *ast.ExprStmt:
		if ta, ok := astUnparen(assign.X).(*ast.TypeAssertExpr); ok {
			return ta.X
		}
	case *ast.AssignStmt:
		if len(assign.Rhs) == 1 {
			if ta, ok := astUnparen(assign.Rhs[0]).(*ast.TypeAssertExpr); ok {
				return ta.X
			}
		}
	}
	return nil
}
