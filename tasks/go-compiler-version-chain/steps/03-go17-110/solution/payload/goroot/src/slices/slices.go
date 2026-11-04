// Package slices defines various functions useful with slices of any type.
package slices

import "cmp"

func Equal[S ~[]E, E comparable](s1, s2 S) bool {
	if len(s1) != len(s2) {
		return false
	}
	for i := range s1 {
		if s1[i] != s2[i] {
			return false
		}
	}
	return true
}

func Index[S ~[]E, E comparable](s S, v E) int {
	for i := range s {
		if s[i] == v {
			return i
		}
	}
	return -1
}

func IndexFunc[S ~[]E, E any](s S, f func(E) bool) int {
	for i := range s {
		if f(s[i]) {
			return i
		}
	}
	return -1
}

func Contains[S ~[]E, E comparable](s S, v E) bool { return Index(s, v) >= 0 }

func ContainsFunc[S ~[]E, E any](s S, f func(E) bool) bool { return IndexFunc(s, f) >= 0 }

func Clone[S ~[]E, E any](s S) S {
	if s == nil {
		return nil
	}
	out := make(S, len(s))
	copy(out, s)
	return out
}

func Reverse[S ~[]E, E any](s S) {
	for i, j := 0, len(s)-1; i < j; i, j = i+1, j-1 {
		s[i], s[j] = s[j], s[i]
	}
}

func Max[S ~[]E, E cmp.Ordered](s S) E {
	if len(s) == 0 {
		panic("slices.Max: empty list")
	}
	m := s[0]
	for _, v := range s[1:] {
		if v > m {
			m = v
		}
	}
	return m
}

func Min[S ~[]E, E cmp.Ordered](s S) E {
	if len(s) == 0 {
		panic("slices.Min: empty list")
	}
	m := s[0]
	for _, v := range s[1:] {
		if v < m {
			m = v
		}
	}
	return m
}

func insertionSortFunc[S ~[]E, E any](s S, less func(a, b E) int) {
	for i := 1; i < len(s); i++ {
		for j := i; j > 0 && less(s[j], s[j-1]) < 0; j-- {
			s[j], s[j-1] = s[j-1], s[j]
		}
	}
}

func Sort[S ~[]E, E cmp.Ordered](s S) {
	insertionSortFunc(s, func(a, b E) int { return cmp.Compare(a, b) })
}

func SortFunc[S ~[]E, E any](s S, cmpf func(a, b E) int) { insertionSortFunc(s, cmpf) }

func SortStableFunc[S ~[]E, E any](s S, cmpf func(a, b E) int) { insertionSortFunc(s, cmpf) }

func IsSorted[S ~[]E, E cmp.Ordered](s S) bool {
	for i := 1; i < len(s); i++ {
		if s[i] < s[i-1] {
			return false
		}
	}
	return true
}

func BinarySearch[S ~[]E, E cmp.Ordered](s S, target E) (int, bool) {
	lo, hi := 0, len(s)
	for lo < hi {
		mid := int(uint(lo+hi) >> 1)
		if s[mid] < target {
			lo = mid + 1
		} else {
			hi = mid
		}
	}
	return lo, lo < len(s) && s[lo] == target
}

func Compact[S ~[]E, E comparable](s S) S {
	if len(s) < 2 {
		return s
	}
	k := 1
	for i := 1; i < len(s); i++ {
		if s[i] != s[k-1] {
			s[k] = s[i]
			k++
		}
	}
	return s[:k]
}

func Insert[S ~[]E, E any](s S, i int, v ...E) S {
	out := make(S, 0, len(s)+len(v))
	out = append(out, s[:i]...)
	out = append(out, v...)
	out = append(out, s[i:]...)
	return out
}

func Delete[S ~[]E, E any](s S, i, j int) S { return append(s[:i], s[j:]...) }

// All returns an iterator over index-value pairs in the slice.
func All[S ~[]E, E any](s S) func(func(int, E) bool) {
	return func(yield func(int, E) bool) {
		for i, v := range s {
			if !yield(i, v) {
				return
			}
		}
	}
}

// Values returns an iterator over the slice elements.
func Values[S ~[]E, E any](s S) func(func(E) bool) {
	return func(yield func(E) bool) {
		for _, v := range s {
			if !yield(v) {
				return
			}
		}
	}
}

// Collect collects values from seq into a new slice.
func Collect[E any](seq func(func(E) bool)) []E {
	var out []E
	seq(func(v E) bool {
		out = append(out, v)
		return true
	})
	return out
}

// Sorted collects values from seq into a new slice and sorts it.
func Sorted[E cmp.Ordered](seq func(func(E) bool)) []E {
	out := Collect(seq)
	Sort(out)
	return out
}

// SortedFunc is like Sorted but with a comparison function.
func SortedFunc[E any](seq func(func(E) bool), cmpf func(E, E) int) []E {
	out := Collect(seq)
	SortFunc(out, cmpf)
	return out
}

// Backward returns an iterator that walks the slice in reverse.
func Backward[S ~[]E, E any](s S) func(func(int, E) bool) {
	return func(yield func(int, E) bool) {
		for i := len(s) - 1; i >= 0; i-- {
			if !yield(i, s[i]) {
				return
			}
		}
	}
}

// Chunk returns an iterator over consecutive sub-slices of up to n elements of
// s. All but the last sub-slice will have size n. Added in Go 1.23.
func Chunk[S ~[]E, E any](s S, n int) func(func(S) bool) {
	if n < 1 {
		panic("cannot be less than 1")
	}
	return func(yield func(S) bool) {
		for i := 0; i < len(s); i += n {
			end := i + n
			if end > len(s) {
				end = len(s)
			}
			if !yield(s[i:end:end]) {
				return
			}
		}
	}
}

// BinarySearchFunc searches for target in a sorted slice using cmpf.
func BinarySearchFunc[S ~[]E, E, T any](s S, target T, cmpf func(E, T) int) (int, bool) {
	lo, hi := 0, len(s)
	for lo < hi {
		mid := int(uint(lo+hi) >> 1)
		if cmpf(s[mid], target) < 0 {
			lo = mid + 1
		} else {
			hi = mid
		}
	}
	return lo, lo < len(s) && cmpf(s[lo], target) == 0
}

// Compare compares two slices elementwise, then by length.
func Compare[S ~[]E, E cmp.Ordered](a, b S) int {
	for i := 0; i < len(a) && i < len(b); i++ {
		if c := cmp.Compare(a[i], b[i]); c != 0 {
			return c
		}
	}
	return cmp.Compare(len(a), len(b))
}

// CompareFunc is Compare with a user-supplied comparison.
func CompareFunc[S1 ~[]E1, S2 ~[]E2, E1, E2 any](a S1, b S2, cmpf func(E1, E2) int) int {
	for i := 0; i < len(a) && i < len(b); i++ {
		if c := cmpf(a[i], b[i]); c != 0 {
			return c
		}
	}
	return cmp.Compare(len(a), len(b))
}

// Clip removes unused capacity from the slice.
func Clip[S ~[]E, E any](s S) S { return s[:len(s):len(s)] }

// Grow increases the slice's capacity by at least n elements.
func Grow[S ~[]E, E any](s S, n int) S {
	if n < 0 {
		panic("cannot be negative")
	}
	if cap(s)-len(s) < n {
		out := make(S, len(s), len(s)+n)
		copy(out, s)
		return out
	}
	return s
}

// Replace replaces s[i:j] with the given values.
func Replace[S ~[]E, E any](s S, i, j int, v ...E) S {
	out := make(S, 0, len(s)-(j-i)+len(v))
	out = append(out, s[:i]...)
	out = append(out, v...)
	out = append(out, s[j:]...)
	return out
}

// Repeat returns a new slice repeating x count times. Added in Go 1.23.
func Repeat[S ~[]E, E any](x S, count int) S {
	if count < 0 {
		panic("cannot be negative")
	}
	out := make(S, 0, len(x)*count)
	for i := 0; i < count; i++ {
		out = append(out, x...)
	}
	return out
}

// CompactFunc is Compact using an equality predicate.
func CompactFunc[S ~[]E, E any](s S, eq func(E, E) bool) S {
	if len(s) < 2 {
		return s
	}
	out := s[:1]
	for _, v := range s[1:] {
		if !eq(out[len(out)-1], v) {
			out = append(out, v)
		}
	}
	return out
}

// Concat returns a new slice concatenating the passed in slices. Added in
// Go 1.22.
func Concat[S ~[]E, E any](ss ...S) S {
	n := 0
	for _, s := range ss {
		n += len(s)
	}
	if n == 0 {
		return nil
	}
	out := make(S, 0, n)
	for _, s := range ss {
		out = append(out, s...)
	}
	return out
}
