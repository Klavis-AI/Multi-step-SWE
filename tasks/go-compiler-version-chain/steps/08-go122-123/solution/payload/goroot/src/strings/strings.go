// Package strings implements simple functions to manipulate UTF-8 encoded strings.
package strings

import "unicode/utf8"

func Count(s, substr string) int {
	if substr == "" {
		return utf8.RuneCountInString(s) + 1
	}
	n := 0
	for i := 0; i+len(substr) <= len(s); {
		if s[i:i+len(substr)] == substr {
			n++
			i += len(substr)
		} else {
			i++
		}
	}
	return n
}

func Index(s, substr string) int {
	n := len(substr)
	if n == 0 {
		return 0
	}
	for i := 0; i+n <= len(s); i++ {
		if s[i:i+n] == substr {
			return i
		}
	}
	return -1
}

func LastIndex(s, substr string) int {
	n := len(substr)
	if n == 0 {
		return len(s)
	}
	for i := len(s) - n; i >= 0; i-- {
		if s[i:i+n] == substr {
			return i
		}
	}
	return -1
}

func IndexByte(s string, c byte) int {
	for i := 0; i < len(s); i++ {
		if s[i] == c {
			return i
		}
	}
	return -1
}

func IndexRune(s string, r rune) int { return Index(s, string(r)) }

func Contains(s, substr string) bool { return Index(s, substr) >= 0 }

func ContainsRune(s string, r rune) bool { return IndexRune(s, r) >= 0 }

func ContainsAny(s, chars string) bool {
	for _, r := range chars {
		if ContainsRune(s, r) {
			return true
		}
	}
	return false
}

func HasPrefix(s, prefix string) bool {
	return len(s) >= len(prefix) && s[:len(prefix)] == prefix
}

func HasSuffix(s, suffix string) bool {
	return len(s) >= len(suffix) && s[len(s)-len(suffix):] == suffix
}

func TrimPrefix(s, prefix string) string {
	if HasPrefix(s, prefix) {
		return s[len(prefix):]
	}
	return s
}

func TrimSuffix(s, suffix string) string {
	if HasSuffix(s, suffix) {
		return s[:len(s)-len(suffix)]
	}
	return s
}

func Repeat(s string, count int) string {
	if count < 0 {
		panic("strings: negative Repeat count")
	}
	b := make([]byte, 0, len(s)*count)
	for i := 0; i < count; i++ {
		b = append(b, s...)
	}
	return string(b)
}

func Split(s, sep string) []string {
	if sep == "" {
		out := []string{}
		for _, r := range s {
			out = append(out, string(r))
		}
		return out
	}
	out := []string{}
	for {
		i := Index(s, sep)
		if i < 0 {
			out = append(out, s)
			return out
		}
		out = append(out, s[:i])
		s = s[i+len(sep):]
	}
}

func SplitN(s, sep string, n int) []string {
	if n == 0 {
		return nil
	}
	if n < 0 {
		return Split(s, sep)
	}
	out := []string{}
	for len(out) < n-1 {
		i := Index(s, sep)
		if i < 0 {
			break
		}
		out = append(out, s[:i])
		s = s[i+len(sep):]
	}
	out = append(out, s)
	return out
}

func Join(elems []string, sep string) string {
	if len(elems) == 0 {
		return ""
	}
	s := elems[0]
	for _, e := range elems[1:] {
		s += sep + e
	}
	return s
}

func Replace(s, old, new string, n int) string {
	if old == "" || n == 0 {
		return s
	}
	out := ""
	for n != 0 {
		i := Index(s, old)
		if i < 0 {
			break
		}
		out += s[:i] + new
		s = s[i+len(old):]
		if n > 0 {
			n--
		}
	}
	return out + s
}

func ReplaceAll(s, old, new string) string { return Replace(s, old, new, -1) }

func ToUpper(s string) string {
	b := make([]byte, 0, len(s))
	for _, r := range s {
		if r >= 'a' && r <= 'z' {
			r -= 32
		}
		b = append(b, string(r)...)
	}
	return string(b)
}

func ToLower(s string) string {
	b := make([]byte, 0, len(s))
	for _, r := range s {
		if r >= 'A' && r <= 'Z' {
			r += 32
		}
		b = append(b, string(r)...)
	}
	return string(b)
}

func isSpace(r rune) bool {
	return r == ' ' || r == '\t' || r == '\n' || r == '\r' || r == '\v' || r == '\f'
}

func TrimSpace(s string) string {
	start := 0
	for start < len(s) && isSpace(rune(s[start])) {
		start++
	}
	end := len(s)
	for end > start && isSpace(rune(s[end-1])) {
		end--
	}
	return s[start:end]
}

func Trim(s, cutset string) string { return TrimLeft(TrimRight(s, cutset), cutset) }

func TrimLeft(s, cutset string) string {
	for len(s) > 0 && ContainsRune(cutset, rune(s[0])) {
		s = s[1:]
	}
	return s
}

func TrimRight(s, cutset string) string {
	for len(s) > 0 && ContainsRune(cutset, rune(s[len(s)-1])) {
		s = s[:len(s)-1]
	}
	return s
}

func Fields(s string) []string {
	out := []string{}
	cur := ""
	for _, r := range s {
		if isSpace(r) {
			if cur != "" {
				out = append(out, cur)
				cur = ""
			}
		} else {
			cur += string(r)
		}
	}
	if cur != "" {
		out = append(out, cur)
	}
	return out
}

func EqualFold(s, t string) bool { return ToLower(s) == ToLower(t) }

func Compare(a, b string) int {
	if a == b {
		return 0
	}
	if a < b {
		return -1
	}
	return 1
}

func Cut(s, sep string) (before, after string, found bool) {
	if i := Index(s, sep); i >= 0 {
		return s[:i], s[i+len(sep):], true
	}
	return s, "", false
}

// Builder is used to efficiently build a string using Write methods.
type Builder struct {
	buf []byte
}

func (b *Builder) WriteString(s string) (int, error) {
	b.buf = append(b.buf, s...)
	return len(s), nil
}
func (b *Builder) Write(p []byte) (int, error) {
	b.buf = append(b.buf, p...)
	return len(p), nil
}
func (b *Builder) WriteByte(c byte) error {
	b.buf = append(b.buf, c)
	return nil
}
func (b *Builder) WriteRune(r rune) (int, error) {
	s := string(r)
	b.buf = append(b.buf, s...)
	return len(s), nil
}
func (b *Builder) String() string { return string(b.buf) }
func (b *Builder) Len() int       { return len(b.buf) }
func (b *Builder) Reset()         { b.buf = nil }
func (b *Builder) Grow(n int)     {}

// SplitAfter slices s after each instance of sep.
func SplitAfter(s, sep string) []string {
	if sep == "" {
		return Split(s, sep)
	}
	var out []string
	for {
		i := Index(s, sep)
		if i < 0 {
			out = append(out, s)
			return out
		}
		out = append(out, s[:i+len(sep)])
		s = s[i+len(sep):]
	}
}

// Lines returns an iterator over the newline-terminated lines of s. Each line
// keeps its trailing newline; the last line has one only if s ends with one.
// Added in Go 1.24.
func Lines(s string) func(func(string) bool) {
	return func(yield func(string) bool) {
		for len(s) > 0 {
			i := IndexByte(s, '\n')
			var line string
			if i < 0 {
				line, s = s, ""
			} else {
				line, s = s[:i+1], s[i+1:]
			}
			if !yield(line) {
				return
			}
		}
	}
}

// SplitSeq is Split as an iterator. Added in Go 1.24.
func SplitSeq(s, sep string) func(func(string) bool) {
	return func(yield func(string) bool) {
		for _, p := range Split(s, sep) {
			if !yield(p) {
				return
			}
		}
	}
}

// SplitAfterSeq is SplitAfter as an iterator. Added in Go 1.24.
func SplitAfterSeq(s, sep string) func(func(string) bool) {
	return func(yield func(string) bool) {
		for _, p := range SplitAfter(s, sep) {
			if !yield(p) {
				return
			}
		}
	}
}

// FieldsSeq is Fields as an iterator. Added in Go 1.24.
func FieldsSeq(s string) func(func(string) bool) {
	return func(yield func(string) bool) {
		for _, f := range Fields(s) {
			if !yield(f) {
				return
			}
		}
	}
}

// FieldsFuncSeq is FieldsFunc as an iterator. Added in Go 1.24.
func FieldsFuncSeq(s string, f func(rune) bool) func(func(string) bool) {
	return func(yield func(string) bool) {
		start := -1
		for i, r := range s {
			if f(r) {
				if start >= 0 {
					if !yield(s[start:i]) {
						return
					}
					start = -1
				}
			} else if start < 0 {
				start = i
			}
		}
		if start >= 0 {
			yield(s[start:])
		}
	}
}
