// Package unique provides facilities for canonicalizing values.
package unique

// A Handle is a globally unique identity for some value of type T.
type Handle[T comparable] struct {
	value *T
}

// Value returns a shallow copy of the value held by the handle.
func (h Handle[T]) Value() T { return *h.value }

var pool = map[any]any{}

// Make returns a globally unique handle for a value of type T. Handles are
// equal if and only if the values they were made from are equal.
func Make[T comparable](value T) Handle[T] {
	if h, ok := pool[value]; ok {
		return h.(Handle[T])
	}
	v := value
	h := Handle[T]{&v}
	pool[value] = h
	return h
}
