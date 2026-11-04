// Package sort provides primitives for sorting slices and user-defined collections.
package sort

// An implementation of Interface can be sorted by the routines in this package.
type Interface interface {
	Len() int
	Less(i, j int) bool
	Swap(i, j int)
}

func insertionSort(data Interface, a, b int) {
	for i := a + 1; i < b; i++ {
		for j := i; j > a && data.Less(j, j-1); j-- {
			data.Swap(j, j-1)
		}
	}
}

func siftDown(data Interface, lo, hi, first int) {
	root := lo
	for {
		child := 2*root + 1
		if child >= hi {
			break
		}
		if child+1 < hi && data.Less(first+child, first+child+1) {
			child++
		}
		if !data.Less(first+root, first+child) {
			return
		}
		data.Swap(first+root, first+child)
		root = child
	}
}

func heapSort(data Interface, a, b int) {
	first := a
	lo := 0
	hi := b - a
	for i := (hi - 1) / 2; i >= 0; i-- {
		siftDown(data, i, hi, first)
	}
	for i := hi - 1; i >= 0; i-- {
		data.Swap(first, first+i)
		siftDown(data, lo, i, first)
	}
}

// Sort sorts data in ascending order as determined by the Less method.
func Sort(data Interface) {
	n := data.Len()
	if n < 12 {
		insertionSort(data, 0, n)
		return
	}
	heapSort(data, 0, n)
}

// Stable sorts data while keeping the original order of equal elements.
func Stable(data Interface) { insertionSort(data, 0, data.Len()) }

func IsSorted(data Interface) bool {
	for i := data.Len() - 1; i > 0; i-- {
		if data.Less(i, i-1) {
			return false
		}
	}
	return true
}

type IntSlice []int

func (x IntSlice) Len() int           { return len(x) }
func (x IntSlice) Less(i, j int) bool { return x[i] < x[j] }
func (x IntSlice) Swap(i, j int)      { x[i], x[j] = x[j], x[i] }

type StringSlice []string

func (x StringSlice) Len() int           { return len(x) }
func (x StringSlice) Less(i, j int) bool { return x[i] < x[j] }
func (x StringSlice) Swap(i, j int)      { x[i], x[j] = x[j], x[i] }

type Float64Slice []float64

func (x Float64Slice) Len() int           { return len(x) }
func (x Float64Slice) Less(i, j int) bool { return x[i] < x[j] || (isNaN(x[i]) && !isNaN(x[j])) }
func (x Float64Slice) Swap(i, j int)      { x[i], x[j] = x[j], x[i] }

func isNaN(f float64) bool { return f != f }

func Ints(x []int)          { Sort(IntSlice(x)) }
func Strings(x []string)    { Sort(StringSlice(x)) }
func Float64s(x []float64)  { Sort(Float64Slice(x)) }
func IntsAreSorted(x []int) bool { return IsSorted(IntSlice(x)) }
func StringsAreSorted(x []string) bool { return IsSorted(StringSlice(x)) }

func Search(n int, f func(int) bool) int {
	i, j := 0, n
	for i < j {
		h := int(uint(i+j) >> 1)
		if !f(h) {
			i = h + 1
		} else {
			j = h
		}
	}
	return i
}

func SearchInts(a []int, x int) int { return Search(len(a), func(i int) bool { return a[i] >= x }) }
func SearchStrings(a []string, x string) int {
	return Search(len(a), func(i int) bool { return a[i] >= x })
}

type lessSwap struct {
	length int
	less   func(i, j int) bool
	swap   func(i, j int)
}

func (l lessSwap) Len() int           { return l.length }
func (l lessSwap) Less(i, j int) bool { return l.less(i, j) }
func (l lessSwap) Swap(i, j int)      { l.swap(i, j) }

// Slice sorts the slice x given the provided less function.
func Slice(x any, less func(i, j int) bool) {
	Sort(lessSwap{sliceLen(x), less, func(i, j int) { sliceSwap(x, i, j) }})
}

// SliceStable sorts the slice x keeping equal elements in their original order.
func SliceStable(x any, less func(i, j int) bool) {
	Stable(lessSwap{sliceLen(x), less, func(i, j int) { sliceSwap(x, i, j) }})
}

func SliceIsSorted(x any, less func(i, j int) bool) bool {
	return IsSorted(lessSwap{sliceLen(x), less, func(i, j int) { sliceSwap(x, i, j) }})
}

func sliceLen(x any) int
func sliceSwap(x any, i, j int)
