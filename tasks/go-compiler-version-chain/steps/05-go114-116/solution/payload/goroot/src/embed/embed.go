// Package embed provides access to files embedded in the running Go program.
package embed

import (
	"errors"
	"io/fs"
	"strings"
)

// An FS is a read-only collection of files, populated by a //go:embed
// directive on a variable of this type.
type FS struct {
	names []string
	datas []string
}

// newFS is called by the generated initialiser for an embedded variable.
func newFS(names, datas []string) FS { return FS{names, datas} }

type embedFile struct {
	name string
	data string
	off  int
	dir  bool
}

func (f *embedFile) Read(p []byte) (int, error) {
	if f.off >= len(f.data) {
		return 0, errors.New("EOF")
	}
	n := copy(p, f.data[f.off:])
	f.off += n
	return n, nil
}
func (f *embedFile) Close() error              { return nil }
func (f *embedFile) Stat() (fs.FileInfo, error) { return &embedInfo{f.name, int64(len(f.data)), f.dir}, nil }

type embedInfo struct {
	name string
	size int64
	dir  bool
}

func (i *embedInfo) Name() string { return baseName(i.name) }
func (i *embedInfo) Size() int64  { return i.size }
func (i *embedInfo) IsDir() bool  { return i.dir }

type embedEntry struct {
	name string
	size int64
	dir  bool
}

func (e *embedEntry) Name() string                { return e.name }
func (e *embedEntry) IsDir() bool                 { return e.dir }
func (e *embedEntry) Info() (fs.FileInfo, error)  { return &embedInfo{e.name, e.size, e.dir}, nil }

func baseName(p string) string {
	if i := strings.LastIndex(p, "/"); i >= 0 {
		return p[i+1:]
	}
	return p
}

func (f FS) ReadFile(name string) ([]byte, error) {
	for i, n := range f.names {
		if n == name {
			return []byte(f.datas[i]), nil
		}
	}
	return nil, &fsPathError{"open", name}
}

type fsPathError struct{ op, path string }

func (e *fsPathError) Error() string { return e.op + " " + e.path + ": file does not exist" }
func (e *fsPathError) Unwrap() error { return fs.ErrNotExist }

func (f FS) Open(name string) (fs.File, error) {
	if !fs.ValidPath(name) {
		return nil, &fsPathError{"open", name}
	}
	for i, n := range f.names {
		if n == name {
			return &embedFile{name: n, data: f.datas[i]}, nil
		}
	}
	if name == "." && len(f.names) > 0 {
		return &embedFile{name: ".", dir: true}, nil
	}
	for _, n := range f.names {
		if strings.HasPrefix(n, name+"/") {
			return &embedFile{name: name, dir: true}, nil
		}
	}
	return nil, &fsPathError{"open", name}
}

func (f FS) ReadDir(name string) ([]fs.DirEntry, error) {
	prefix := ""
	if name != "." {
		prefix = name + "/"
	}
	seen := map[string]bool{}
	var out []fs.DirEntry
	for i, n := range f.names {
		if !strings.HasPrefix(n, prefix) {
			continue
		}
		rest := n[len(prefix):]
		if rest == "" {
			continue
		}
		if j := strings.Index(rest, "/"); j >= 0 {
			d := rest[:j]
			if !seen[d] {
				seen[d] = true
				out = append(out, &embedEntry{name: d, dir: true})
			}
			continue
		}
		if !seen[rest] {
			seen[rest] = true
			out = append(out, &embedEntry{name: rest, size: int64(len(f.datas[i]))})
		}
	}
	if out == nil {
		return nil, &fsPathError{"open", name}
	}
	// directory entries are returned sorted by filename
	for i := 1; i < len(out); i++ {
		for j := i; j > 0 && out[j].Name() < out[j-1].Name(); j-- {
			out[j], out[j-1] = out[j-1], out[j]
		}
	}
	return out, nil
}
