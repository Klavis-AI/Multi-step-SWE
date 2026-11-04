// Package os provides a platform-independent interface to operating system functionality.
package os

import "errors"

func write(fd int, b []byte) int
func exitProcess(code int)
func getenv(k string) (string, bool)
func args() []string
func readFile(name string) ([]byte, bool)

// File represents an open file descriptor.
type File struct {
	fd   int
	name string
}

func (f *File) Write(b []byte) (int, error)      { return write(f.fd, b), nil }
func (f *File) WriteString(s string) (int, error) { return write(f.fd, []byte(s)), nil }
func (f *File) Name() string                      { return f.name }
func (f *File) Close() error                      { return nil }

var Stdout = &File{1, "/dev/stdout"}
var Stderr = &File{2, "/dev/stderr"}
var Stdin = &File{0, "/dev/stdin"}

var Args = args()

func Exit(code int) { exitProcess(code) }

func Getenv(key string) string { v, _ := getenv(key); return v }

func LookupEnv(key string) (string, bool) { return getenv(key) }

var ErrNotExist = errors.New("file does not exist")

func ReadFile(name string) ([]byte, error) {
	b, ok := readFile(name)
	if !ok {
		return nil, errors.New("open " + name + ": no such file or directory")
	}
	return b, nil
}
