// Package iter provides basic definitions and operations related to iterators
// over sequences.
package iter

// Seq is an iterator over sequences of individual values.
type Seq[V any] func(yield func(V) bool)

// Seq2 is an iterator over sequences of pairs of values.
type Seq2[K, V any] func(yield func(K, V) bool)
