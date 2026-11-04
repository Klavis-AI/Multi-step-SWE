// Package fs defines basic interfaces to a file system.
package fs

import (
	"errors"
	"sort"
)

// An FS provides access to a hierarchical file system.
type FS interface {
	Open(name string) (File, error)
}

type File interface {
	Stat() (FileInfo, error)
	Read([]byte) (int, error)
	Close() error
}

type FileInfo interface {
	Name() string
	Size() int64
	IsDir() bool
}

type DirEntry interface {
	Name() string
	IsDir() bool
	Info() (FileInfo, error)
}

type ReadDirFS interface {
	FS
	ReadDir(name string) ([]DirEntry, error)
}

type ReadFileFS interface {
	FS
	ReadFile(name string) ([]byte, error)
}

var ErrNotExist = errors.New("file does not exist")
var ErrInvalid = errors.New("invalid argument")

// ValidPath reports whether the given path name is valid for use in a call to
// Open: unrooted, slash-separated, and free of "." and ".." elements.
func ValidPath(name string) bool {
	if name == "." {
		return true
	}
	if name == "" || name[0] == '/' || name[len(name)-1] == '/' {
		return false
	}
	start := 0
	for i := 0; i <= len(name); i++ {
		if i == len(name) || name[i] == '/' {
			elem := name[start:i]
			if elem == "" || elem == "." || elem == ".." {
				return false
			}
			start = i + 1
		}
	}
	return true
}

func ReadFile(fsys FS, name string) ([]byte, error) {
	if r, ok := fsys.(ReadFileFS); ok {
		return r.ReadFile(name)
	}
	return nil, ErrInvalid
}

func ReadDir(fsys FS, name string) ([]DirEntry, error) {
	if r, ok := fsys.(ReadDirFS); ok {
		d, err := r.ReadDir(name)
		if err != nil {
			return nil, err
		}
		sort.Slice(d, func(i, j int) bool { return d[i].Name() < d[j].Name() })
		return d, nil
	}
	return nil, ErrInvalid
}

// WalkDirFunc is the type of the function called by WalkDir.
type WalkDirFunc func(path string, d DirEntry, err error) error

var SkipDir = errors.New("skip this directory")

type statEntry struct {
	name string
	size int64
	dir  bool
}

func (e *statEntry) Name() string               { return e.name }
func (e *statEntry) IsDir() bool                { return e.dir }
func (e *statEntry) Info() (FileInfo, error)    { return &statInfo{e.name, e.size, e.dir}, nil }

type statInfo struct {
	name string
	size int64
	dir  bool
}

func (i *statInfo) Name() string { return i.name }
func (i *statInfo) Size() int64  { return i.size }
func (i *statInfo) IsDir() bool  { return i.dir }

func baseOf(p string) string {
	for i := len(p) - 1; i >= 0; i-- {
		if p[i] == '/' {
			return p[i+1:]
		}
	}
	return p
}

// WalkDir walks the file tree rooted at root, calling fn for each file or
// directory in the tree, including root itself.
func WalkDir(fsys FS, root string, fn WalkDirFunc) error {
	f, err := fsys.Open(root)
	if err != nil {
		return fn(root, nil, err)
	}
	info, err := f.Stat()
	f.Close()
	if err != nil {
		return fn(root, nil, err)
	}
	entry := &statEntry{baseOf(root), info.Size(), info.IsDir()}
	if err := fn(root, entry, nil); err != nil {
		if err == SkipDir {
			return nil
		}
		return err
	}
	if !info.IsDir() {
		return nil
	}
	return walkDirChildren(fsys, root, fn)
}

func walkDirChildren(fsys FS, root string, fn WalkDirFunc) error {
	entries, err := ReadDir(fsys, root)
	if err != nil {
		return fn(root, nil, err)
	}
	for _, e := range entries {
		p := e.Name()
		if root != "." {
			p = root + "/" + e.Name()
		}
		if err := fn(p, e, nil); err != nil {
			if err == SkipDir && e.IsDir() {
				continue
			}
			return err
		}
		if e.IsDir() {
			if err := walkDirChildren(fsys, p, fn); err != nil {
				return err
			}
		}
	}
	return nil
}

// SubFS is implemented by a file system that can produce a sub-tree itself.
type SubFS interface {
	FS
	Sub(dir string) (FS, error)
}

type subFS struct {
	parent FS
	dir    string
}

func (s *subFS) full(name string) string {
	if name == "." {
		return s.dir
	}
	return s.dir + "/" + name
}

func (s *subFS) Open(name string) (File, error) {
	if !ValidPath(name) {
		return nil, &pathError{"sub", name}
	}
	return s.parent.Open(s.full(name))
}

func (s *subFS) ReadDir(name string) ([]DirEntry, error) { return ReadDir(s.parent, s.full(name)) }

func (s *subFS) ReadFile(name string) ([]byte, error) { return ReadFile(s.parent, s.full(name)) }

type pathError struct{ op, path string }

func (e *pathError) Error() string { return e.op + " " + e.path + ": invalid argument" }

// Sub returns an FS rooted at dir within fsys.
func Sub(fsys FS, dir string) (FS, error) {
	if !ValidPath(dir) {
		return nil, &pathError{"sub", dir}
	}
	if dir == "." {
		return fsys, nil
	}
	if sf, ok := fsys.(SubFS); ok {
		return sf.Sub(dir)
	}
	return &subFS{fsys, dir}, nil
}

// GlobFS is implemented by a file system that can do its own matching.
type GlobFS interface {
	FS
	Glob(pattern string) ([]string, error)
}

// Glob returns the names matching pattern, in lexical order. Only the `*`
// wildcard is meaningful here, and it does not cross a path separator.
func Glob(fsys FS, pattern string) ([]string, error) {
	if gf, ok := fsys.(GlobFS); ok {
		return gf.Glob(pattern)
	}
	var out []string
	err := WalkDir(fsys, ".", func(p string, d DirEntry, err error) error {
		if err != nil {
			return err
		}
		if p != "." && match(pattern, p) {
			out = append(out, p)
		}
		return nil
	})
	if err != nil {
		return nil, err
	}
	return out, nil
}

// match reports whether name matches pattern, segment by segment.
func match(pattern, name string) bool {
	ps := split(pattern)
	ns := split(name)
	if len(ps) != len(ns) {
		return false
	}
	for i := range ps {
		if !matchSegment(ps[i], ns[i]) {
			return false
		}
	}
	return true
}

func split(p string) []string {
	var out []string
	start := 0
	for i := 0; i < len(p); i++ {
		if p[i] == '/' {
			out = append(out, p[start:i])
			start = i + 1
		}
	}
	return append(out, p[start:])
}

func matchSegment(pat, s string) bool {
	if pat == "*" {
		return true
	}
	pi, si := 0, 0
	star, mark := -1, 0
	for si < len(s) {
		if pi < len(pat) && (pat[pi] == '?' || pat[pi] == s[si]) {
			pi++
			si++
		} else if pi < len(pat) && pat[pi] == '*' {
			star = pi
			mark = si
			pi++
		} else if star >= 0 {
			pi = star + 1
			mark++
			si = mark
		} else {
			return false
		}
	}
	for pi < len(pat) && pat[pi] == '*' {
		pi++
	}
	return pi == len(pat)
}
