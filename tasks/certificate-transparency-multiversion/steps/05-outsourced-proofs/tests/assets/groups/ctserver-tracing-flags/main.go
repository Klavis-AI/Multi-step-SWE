// Group ctserver-tracing-flags.
//
// v1.0.17 wires opencensus Stackdriver tracing into ct_server (commits 88f78d6c
// and fe719e00), exposed through three flags. Only the server's own usage text
// is read here: the three names, and the type token the flag package prints
// after each one. That token is not free decoration -- it is derived from the
// declared flag type, so it distinguishes CLI.md section 2.1's Bool/String/Int
// columns from each other, and a tracing_percent declared as a string (or a
// tracing declared as anything but a bool) fails case 3. No default value and
// no usage wording is asserted anywhere here.
//
// SCOPE-TIME: no clock, no network. The child runs with --help; its output is
// captured, never shared with this process's stdout, and CT_VERIFY_NONCE is
// stripped from its environment by vhelp.Command.
package main

import (
	"bytes"
	"fmt"
	"regexp"
	"strings"

	"ctverify/vhelp"
)

// flagDeclared reports whether the usage text declares exactly this flag name
// (as opposed to a longer name that merely starts with it).
func flagDeclared(usage, flag string) bool {
	re := regexp.MustCompile(`(?m)^\s+-` + regexp.QuoteMeta(flag) + `(\s|$)`)
	return re.MatchString(usage)
}

// flagType returns the type token the flag package prints after a flag name on
// its declaration line: "string" for a String flag, "int" for an Int flag, and
// the empty string for a Bool flag, which is printed bare. It returns "absent"
// if the flag is not declared at all, so a missing flag can never be mistaken
// for a bool.
func flagType(usage, flag string) string {
	for _, line := range strings.Split(usage, "\n") {
		if !flagDeclared(line, flag) {
			continue
		}
		return strings.TrimSpace(strings.TrimPrefix(strings.TrimSpace(line), "-"+flag))
	}
	return "absent"
}

func main() {
	t := vhelp.New()

	cmd := vhelp.Command(vhelp.CandidateBin("ct_server"), "--help")
	var out bytes.Buffer
	cmd.Stdout = &out
	cmd.Stderr = &out
	// Only the usage text is graded here. The exit status of --help is not this
	// group's business -- CLI.md's preamble pins it to 0 and the gate group
	// build-clean asserts it -- so the error is deliberately ignored.
	_ = cmd.Run()

	usage := out.String()
	if usage == "" {
		t.Fatalf("ct_server --help printed nothing")
	}

	// --- 1. --tracing is declared -------------------------------------------
	t.Check("tracing-flag-declared", flagDeclared(usage, "tracing"),
		"ct_server usage does not declare -tracing")

	// --- 2. --tracing_project_id is declared -------------------------------
	t.Check("tracing-project-id-flag-declared", flagDeclared(usage, "tracing_project_id"),
		"ct_server usage does not declare -tracing_project_id")

	// --- 3. ... and all three carry the types CLI.md section 2.1 gives them --
	// Bool prints bare, String prints "string", Int prints "int".
	t.Equal("tracing-flags-carry-their-declared-types",
		fmt.Sprintf("tracing=%q project_id=%q percent=%q",
			flagType(usage, "tracing"),
			flagType(usage, "tracing_project_id"),
			flagType(usage, "tracing_percent")),
		`tracing="" project_id="string" percent="int"`)

	// --- 4. --tracing_percent is declared ------------------------------------
	t.Check("tracing-percent-flag-declared", flagDeclared(usage, "tracing_percent"),
		"ct_server usage does not declare -tracing_percent")

	t.Done()
}
