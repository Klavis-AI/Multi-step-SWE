// Package bytes implements functions for the manipulation of byte slices.
package bytes

import "unicode/utf8"

func Equal(a, b []byte) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

func Compare(a, b []byte) int {
	n := len(a)
	if len(b) < n {
		n = len(b)
	}
	for i := 0; i < n; i++ {
		if a[i] != b[i] {
			if a[i] < b[i] {
				return -1
			}
			return 1
		}
	}
	if len(a) == len(b) {
		return 0
	}
	if len(a) < len(b) {
		return -1
	}
	return 1
}

func Index(s, sep []byte) int {
	n := len(sep)
	if n == 0 {
		return 0
	}
	for i := 0; i+n <= len(s); i++ {
		if Equal(s[i:i+n], sep) {
			return i
		}
	}
	return -1
}

func IndexByte(s []byte, c byte) int {
	for i, b := range s {
		if b == c {
			return i
		}
	}
	return -1
}

func Contains(b, subslice []byte) bool { return Index(b, subslice) >= 0 }
func HasPrefix(s, prefix []byte) bool  { return len(s) >= len(prefix) && Equal(s[:len(prefix)], prefix) }
func HasSuffix(s, suffix []byte) bool  { return len(s) >= len(suffix) && Equal(s[len(s)-len(suffix):], suffix) }

func Split(s, sep []byte) [][]byte {
	out := [][]byte{}
	if len(sep) == 0 {
		for i := 0; i < len(s); i++ {
			out = append(out, s[i:i+1])
		}
		return out
	}
	for {
		i := Index(s, sep)
		if i < 0 {
			return append(out, s)
		}
		out = append(out, s[:i])
		s = s[i+len(sep):]
	}
}

func Join(s [][]byte, sep []byte) []byte {
	if len(s) == 0 {
		return []byte{}
	}
	out := append([]byte{}, s[0]...)
	for _, b := range s[1:] {
		out = append(out, sep...)
		out = append(out, b...)
	}
	return out
}

func Repeat(b []byte, count int) []byte {
	out := make([]byte, 0, len(b)*count)
	for i := 0; i < count; i++ {
		out = append(out, b...)
	}
	return out
}

func ToUpper(s []byte) []byte {
	out := make([]byte, len(s))
	for i, c := range s {
		if c >= 'a' && c <= 'z' {
			c -= 32
		}
		out[i] = c
	}
	return out
}

func ToLower(s []byte) []byte {
	out := make([]byte, len(s))
	for i, c := range s {
		if c >= 'A' && c <= 'Z' {
			c += 32
		}
		out[i] = c
	}
	return out
}

func isSpace(c byte) bool {
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'
}

func TrimSpace(s []byte) []byte {
	i := 0
	for i < len(s) && isSpace(s[i]) {
		i++
	}
	j := len(s)
	for j > i && isSpace(s[j-1]) {
		j--
	}
	return s[i:j]
}

func Runes(s []byte) []rune { return []rune(string(s)) }

func Count(s, sep []byte) int {
	if len(sep) == 0 {
		return utf8.RuneCount(s) + 1
	}
	n := 0
	for i := 0; i+len(sep) <= len(s); {
		if Equal(s[i:i+len(sep)], sep) {
			n++
			i += len(sep)
		} else {
			i++
		}
	}
	return n
}

// A Buffer is a variable-sized buffer of bytes with Read and Write methods.
type Buffer struct {
	buf []byte
	off int
}

func NewBuffer(buf []byte) *Buffer         { return &Buffer{buf: buf} }
func NewBufferString(s string) *Buffer     { return &Buffer{buf: []byte(s)} }
func (b *Buffer) Write(p []byte) (int, error) {
	b.buf = append(b.buf, p...)
	return len(p), nil
}
func (b *Buffer) WriteString(s string) (int, error) {
	b.buf = append(b.buf, s...)
	return len(s), nil
}
func (b *Buffer) WriteByte(c byte) error {
	b.buf = append(b.buf, c)
	return nil
}
func (b *Buffer) WriteRune(r rune) (int, error) {
	s := string(r)
	b.buf = append(b.buf, s...)
	return len(s), nil
}
func (b *Buffer) Bytes() []byte  { return b.buf[b.off:] }
func (b *Buffer) String() string { return string(b.buf[b.off:]) }
func (b *Buffer) Len() int       { return len(b.buf) - b.off }
func (b *Buffer) Reset()         { b.buf = b.buf[:0]; b.off = 0 }
func (b *Buffer) Truncate(n int) { b.buf = b.buf[:b.off+n] }

// Lines returns an iterator over the newline-terminated lines of s. Added in
// Go 1.24.
func Lines(s []byte) func(func([]byte) bool) {
	return func(yield func([]byte) bool) {
		for len(s) > 0 {
			i := IndexByte(s, '\n')
			var line []byte
			if i < 0 {
				line, s = s, nil
			} else {
				line, s = s[:i+1], s[i+1:]
			}
			if !yield(line) {
				return
			}
		}
	}
}
