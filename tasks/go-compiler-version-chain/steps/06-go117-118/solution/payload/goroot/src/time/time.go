// Package time provides functionality for measuring and displaying time.
package time

func sleepNanos(d int64)

// A Duration represents the elapsed time between two instants as an int64
// nanosecond count.
type Duration int64

const (
	Nanosecond  Duration = 1
	Microsecond          = 1000 * Nanosecond
	Millisecond          = 1000 * Microsecond
	Second               = 1000 * Millisecond
	Minute               = 60 * Second
	Hour                 = 60 * Minute
)

func (d Duration) Nanoseconds() int64   { return int64(d) }
func (d Duration) Microseconds() int64  { return int64(d) / 1e3 }
func (d Duration) Milliseconds() int64  { return int64(d) / 1e6 }
func (d Duration) Seconds() float64     { return float64(d) / 1e9 }
func (d Duration) Minutes() float64     { return float64(d) / (60 * 1e9) }
func (d Duration) Hours() float64       { return float64(d) / (3600 * 1e9) }

func (d Duration) String() string {
	if d == 0 {
		return "0s"
	}
	neg := d < 0
	u := int64(d)
	if neg {
		u = -u
	}
	var s string
	if u < int64(Second) {
		var prec int64
		var unit string
		switch {
		case u < int64(Microsecond):
			prec, unit = 1, "ns"
		case u < int64(Millisecond):
			prec, unit = 1000, "\u00b5s"
		default:
			prec, unit = 1000000, "ms"
		}
		s = itoa(u/prec) + fmtFrac(u%prec, prec) + unit
	} else {
		sec := u / int64(Second)
		frac := u % int64(Second)
		s = itoa(sec%60) + fmtFrac(frac, int64(Second)) + "s"
		rem := sec / 60
		if rem > 0 {
			s = itoa(rem%60) + "m" + s
			rem /= 60
			if rem > 0 {
				s = itoa(rem) + "h" + s
			}
		}
	}
	if neg {
		s = "-" + s
	}
	return s
}

func itoa(v int64) string {
	if v == 0 {
		return "0"
	}
	buf := make([]byte, 0, 20)
	for v > 0 {
		buf = append([]byte{byte('0' + v%10)}, buf...)
		v /= 10
	}
	return string(buf)
}

func fmtFrac(v, prec int64) string {
	if v == 0 {
		return ""
	}
	digits := ""
	for prec > 1 {
		prec /= 10
		digits += string(byte('0' + (v/prec)%10))
	}
	for len(digits) > 0 && digits[len(digits)-1] == '0' {
		digits = digits[:len(digits)-1]
	}
	if digits == "" {
		return ""
	}
	return "." + digits
}

// Sleep pauses the current goroutine for at least the duration d.
func Sleep(d Duration) { sleepNanos(int64(d)) }

func now() (sec int64, nsec int32, mono int64)

// A Time represents an instant in time with nanosecond precision.
//
// A Time returned by Now also carries a monotonic clock reading. Time
// arithmetic and comparison use the monotonic reading whenever both operands
// have one; Round and Truncate strip it.
type Time struct {
	sec  int64
	nsec int32
	mono int64
}

// Now returns the current local time.
func Now() Time {
	s, ns, m := now()
	return Time{s, ns, m}
}

func (t Time) Unix() int64     { return t.sec }
func (t Time) UnixNano() int64 { return t.sec*1e9 + int64(t.nsec) }
func (t Time) IsZero() bool    { return t.sec == 0 && t.nsec == 0 }

func (t Time) hasMonotonic() bool { return t.mono != 0 }

func (t Time) Sub(u Time) Duration {
	if t.mono != 0 && u.mono != 0 {
		return Duration(t.mono - u.mono)
	}
	return Duration((t.sec-u.sec)*1e9 + int64(t.nsec) - int64(u.nsec))
}

func (t Time) Add(d Duration) Time {
	ns := int64(t.nsec) + int64(d)
	sec := t.sec + ns/1e9
	ns = ns % 1e9
	if ns < 0 {
		ns += 1e9
		sec--
	}
	m := t.mono
	if m != 0 {
		m += int64(d)
	}
	return Time{sec, int32(ns), m}
}

func (t Time) Before(u Time) bool {
	if t.mono != 0 && u.mono != 0 {
		return t.mono < u.mono
	}
	return t.sec < u.sec || (t.sec == u.sec && t.nsec < u.nsec)
}
func (t Time) After(u Time) bool { return u.Before(t) }
func (t Time) Equal(u Time) bool {
	if t.mono != 0 && u.mono != 0 {
		return t.mono == u.mono
	}
	return t.sec == u.sec && t.nsec == u.nsec
}

// Round returns the result of rounding t to the nearest multiple of d.
// Round(0) strips any monotonic clock reading but leaves the wall clock
// reading unchanged.
func (t Time) Round(d Duration) Time {
	t.mono = 0
	if d <= 0 {
		return t
	}
	ns := t.sec*1e9 + int64(t.nsec)
	r := ns % int64(d)
	if r+r < int64(d) {
		ns -= r
	} else {
		ns += int64(d) - r
	}
	return Time{ns / 1e9, int32(ns % 1e9), 0}
}

// Truncate rounds t down to a multiple of d and strips the monotonic reading.
func (t Time) Truncate(d Duration) Time {
	t.mono = 0
	if d <= 0 {
		return t
	}
	ns := t.sec*1e9 + int64(t.nsec)
	ns -= ns % int64(d)
	return Time{ns / 1e9, int32(ns % 1e9), 0}
}

// Since returns the time elapsed since t.
func Since(t Time) Duration { return Now().Sub(t) }

// Until returns the duration until t.
func Until(t Time) Duration { return t.Sub(Now()) }

func (d Duration) Round(m Duration) Duration {
	if m <= 0 {
		return d
	}
	r := d % m
	if d < 0 {
		r = -r
		if r+r < m {
			return d + r
		}
		return d - (m - r)
	}
	if r+r < m {
		return d - r
	}
	return d + (m - r)
}

func (d Duration) Truncate(m Duration) Duration {
	if m <= 0 {
		return d
	}
	return d - d%m
}

// After waits for the duration to elapse and then sends the current time on
// the returned channel.
func After(d Duration) <-chan Time {
	c := make(chan Time, 1)
	go func() {
		Sleep(d)
		c <- Now()
	}()
	return c
}

// Tick is a convenience wrapper for NewTicker providing access to the ticking
// channel only.
func Tick(d Duration) <-chan Time {
	c := make(chan Time, 1)
	go func() {
		for {
			Sleep(d)
			select {
			case c <- Now():
			default:
			}
		}
	}()
	return c
}

// A Timer represents a single event.
type Timer struct {
	C    <-chan Time
	c    chan Time
	stop chan struct{}
}

func NewTimer(d Duration) *Timer {
	c := make(chan Time, 1)
	t := &Timer{C: c, c: c, stop: make(chan struct{}, 1)}
	go func() {
		Sleep(d)
		select {
		case <-t.stop:
		default:
			select {
			case c <- Now():
			default:
			}
		}
	}()
	return t
}

func (t *Timer) Stop() bool {
	select {
	case t.stop <- struct{}{}:
		return true
	default:
		return false
	}
}
