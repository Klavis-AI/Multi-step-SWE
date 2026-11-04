// Package strconv implements conversions to and from string representations of basic data types.
package strconv

import "errors"

func formatFloat(f float64, fmt byte, prec, bitSize int) string
func parseFloat(s string) (float64, bool)

var ErrSyntax = errors.New("invalid syntax")
var ErrRange = errors.New("value out of range")

type NumError struct {
	Func string
	Num  string
	Err  error
}

func (e *NumError) Error() string { return "strconv." + e.Func + ": parsing " + Quote(e.Num) + ": " + e.Err.Error() }
func (e *NumError) Unwrap() error { return e.Err }

const digits = "0123456789abcdefghijklmnopqrstuvwxyz"

func FormatInt(i int64, base int) string {
	if base < 2 || base > 36 {
		panic("strconv: illegal AppendInt/FormatInt base")
	}
	if i == 0 {
		return "0"
	}
	neg := i < 0
	var u uint64
	if neg {
		u = uint64(-(i + 1)) + 1
	} else {
		u = uint64(i)
	}
	return formatUint(u, base, neg)
}

func FormatUint(u uint64, base int) string { return formatUint(u, base, false) }

func formatUint(u uint64, base int, neg bool) string {
	if u == 0 && !neg {
		return "0"
	}
	var buf [70]byte
	n := len(buf)
	b := uint64(base)
	for u > 0 {
		n--
		buf[n] = digits[u%b]
		u /= b
	}
	if neg {
		n--
		buf[n] = '-'
	}
	return string(buf[n:])
}

func Itoa(i int) string { return FormatInt(int64(i), 10) }

func AppendInt(dst []byte, i int64, base int) []byte { return append(dst, FormatInt(i, base)...) }

func ParseInt(s string, base int, bitSize int) (int64, error) {
	orig := s
	if s == "" {
		return 0, &NumError{"ParseInt", orig, ErrSyntax}
	}
	neg := false
	if s[0] == '+' {
		s = s[1:]
	} else if s[0] == '-' {
		neg = true
		s = s[1:]
	}
	if s == "" {
		return 0, &NumError{"ParseInt", orig, ErrSyntax}
	}
	if base == 0 {
		base = 10
		if len(s) > 2 && s[0] == '0' {
			switch s[1] {
			case 'x', 'X':
				base, s = 16, s[2:]
			case 'b', 'B':
				base, s = 2, s[2:]
			case 'o', 'O':
				base, s = 8, s[2:]
			}
		} else if len(s) > 1 && s[0] == '0' {
			base, s = 8, s[1:]
		}
	}
	var n uint64
	for i := 0; i < len(s); i++ {
		c := s[i]
		if c == '_' {
			continue
		}
		var d int
		switch {
		case c >= '0' && c <= '9':
			d = int(c - '0')
		case c >= 'a' && c <= 'z':
			d = int(c-'a') + 10
		case c >= 'A' && c <= 'Z':
			d = int(c-'A') + 10
		default:
			return 0, &NumError{"ParseInt", orig, ErrSyntax}
		}
		if d >= base {
			return 0, &NumError{"ParseInt", orig, ErrSyntax}
		}
		n = n*uint64(base) + uint64(d)
	}
	if neg {
		return -int64(n), nil
	}
	return int64(n), nil
}

func ParseUint(s string, base int, bitSize int) (uint64, error) {
	v, err := ParseInt(s, base, 64)
	if err != nil {
		return 0, err
	}
	return uint64(v), nil
}

func Atoi(s string) (int, error) {
	v, err := ParseInt(s, 10, 64)
	if err != nil {
		if ne, ok := err.(*NumError); ok {
			ne.Func = "Atoi"
		}
		return 0, err
	}
	return int(v), nil
}

func FormatFloat(f float64, fmt byte, prec, bitSize int) string {
	return formatFloat(f, fmt, prec, bitSize)
}

func ParseFloat(s string, bitSize int) (float64, error) {
	v, ok := parseFloat(s)
	if !ok {
		return 0, &NumError{"ParseFloat", s, ErrSyntax}
	}
	return v, nil
}

func FormatBool(b bool) string {
	if b {
		return "true"
	}
	return "false"
}

func ParseBool(str string) (bool, error) {
	switch str {
	case "1", "t", "T", "true", "TRUE", "True":
		return true, nil
	case "0", "f", "F", "false", "FALSE", "False":
		return false, nil
	}
	return false, &NumError{"ParseBool", str, ErrSyntax}
}

const lowerhex = "0123456789abcdef"

func Quote(s string) string {
	buf := make([]byte, 0, len(s)+2)
	buf = append(buf, '"')
	for _, r := range s {
		buf = appendEscapedRune(buf, r, '"')
	}
	buf = append(buf, '"')
	return string(buf)
}

func QuoteRune(r rune) string {
	buf := make([]byte, 0, 8)
	buf = append(buf, '\'')
	buf = appendEscapedRune(buf, r, '\'')
	buf = append(buf, '\'')
	return string(buf)
}

func appendEscapedRune(buf []byte, r rune, quote byte) []byte {
	if r == rune(quote) || r == '\\' {
		return append(buf, '\\', byte(r))
	}
	switch r {
	case '\a':
		return append(buf, '\\', 'a')
	case '\b':
		return append(buf, '\\', 'b')
	case '\f':
		return append(buf, '\\', 'f')
	case '\n':
		return append(buf, '\\', 'n')
	case '\r':
		return append(buf, '\\', 'r')
	case '\t':
		return append(buf, '\\', 't')
	case '\v':
		return append(buf, '\\', 'v')
	}
	if r < 0x20 || r == 0x7f {
		return append(buf, '\\', 'x', lowerhex[byte(r)>>4], lowerhex[byte(r)&0xF])
	}
	return append(buf, string(r)...)
}

func Unquote(s string) (string, error) {
	if len(s) < 2 {
		return "", ErrSyntax
	}
	q := s[0]
	if q != s[len(s)-1] {
		return "", ErrSyntax
	}
	s = s[1 : len(s)-1]
	if q == '`' {
		return s, nil
	}
	if q != '"' && q != '\'' {
		return "", ErrSyntax
	}
	buf := make([]byte, 0, len(s))
	for i := 0; i < len(s); i++ {
		if s[i] != '\\' {
			buf = append(buf, s[i])
			continue
		}
		i++
		if i >= len(s) {
			return "", ErrSyntax
		}
		switch s[i] {
		case 'a':
			buf = append(buf, '\a')
		case 'b':
			buf = append(buf, '\b')
		case 'f':
			buf = append(buf, '\f')
		case 'n':
			buf = append(buf, '\n')
		case 'r':
			buf = append(buf, '\r')
		case 't':
			buf = append(buf, '\t')
		case 'v':
			buf = append(buf, '\v')
		case '\\':
			buf = append(buf, '\\')
		case '\'':
			buf = append(buf, '\'')
		case '"':
			buf = append(buf, '"')
		default:
			return "", ErrSyntax
		}
	}
	return string(buf), nil
}
