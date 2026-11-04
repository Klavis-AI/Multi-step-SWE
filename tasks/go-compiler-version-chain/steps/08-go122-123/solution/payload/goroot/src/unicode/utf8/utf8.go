// Package utf8 implements functions and constants to support text encoded in UTF-8.
package utf8

const (
	RuneError = '�'
	RuneSelf  = 0x80
	MaxRune   = '\U0010FFFF'
	UTFMax    = 4
)

func RuneLen(r rune) int {
	switch {
	case r < 0:
		return -1
	case r <= 0x7F:
		return 1
	case r <= 0x7FF:
		return 2
	case r >= 0xD800 && r <= 0xDFFF:
		return -1
	case r <= 0xFFFF:
		return 3
	case r <= MaxRune:
		return 4
	}
	return -1
}

func EncodeRune(p []byte, r rune) int {
	if uint32(r) <= 0x7F {
		p[0] = byte(r)
		return 1
	}
	if r < 0 || r > MaxRune || (r >= 0xD800 && r <= 0xDFFF) {
		r = RuneError
	}
	switch {
	case uint32(r) <= 0x7FF:
		p[0] = 0xC0 | byte(r>>6)
		p[1] = 0x80 | byte(r)&0x3F
		return 2
	case uint32(r) <= 0xFFFF:
		p[0] = 0xE0 | byte(r>>12)
		p[1] = 0x80 | byte(r>>6)&0x3F
		p[2] = 0x80 | byte(r)&0x3F
		return 3
	}
	p[0] = 0xF0 | byte(r>>18)
	p[1] = 0x80 | byte(r>>12)&0x3F
	p[2] = 0x80 | byte(r>>6)&0x3F
	p[3] = 0x80 | byte(r)&0x3F
	return 4
}

func DecodeRuneInString(s string) (r rune, size int) {
	if len(s) == 0 {
		return RuneError, 0
	}
	c := s[0]
	if c < 0x80 {
		return rune(c), 1
	}
	var n int
	var v rune
	switch {
	case c&0xE0 == 0xC0:
		n, v = 2, rune(c&0x1F)
	case c&0xF0 == 0xE0:
		n, v = 3, rune(c&0x0F)
	case c&0xF8 == 0xF0:
		n, v = 4, rune(c&0x07)
	default:
		return RuneError, 1
	}
	if len(s) < n {
		return RuneError, 1
	}
	for i := 1; i < n; i++ {
		cc := s[i]
		if cc&0xC0 != 0x80 {
			return RuneError, 1
		}
		v = v<<6 | rune(cc&0x3F)
	}
	if (n == 2 && v < 0x80) || (n == 3 && v < 0x800) || (n == 4 && v < 0x10000) || v > MaxRune || (v >= 0xD800 && v <= 0xDFFF) {
		return RuneError, 1
	}
	return v, n
}

func DecodeRune(p []byte) (r rune, size int) { return DecodeRuneInString(string(p)) }

func RuneCountInString(s string) int {
	n := 0
	for range s {
		n++
	}
	return n
}

func RuneCount(p []byte) int { return RuneCountInString(string(p)) }

func ValidString(s string) bool {
	for i := 0; i < len(s); {
		r, size := DecodeRuneInString(s[i:])
		if r == RuneError && size == 1 {
			return false
		}
		i += size
	}
	return true
}

func Valid(p []byte) bool { return ValidString(string(p)) }

func ValidRune(r rune) bool { return RuneLen(r) >= 0 }

func AppendRune(p []byte, r rune) []byte {
	var buf [4]byte
	n := EncodeRune(buf[:], r)
	return append(p, buf[:n]...)
}
