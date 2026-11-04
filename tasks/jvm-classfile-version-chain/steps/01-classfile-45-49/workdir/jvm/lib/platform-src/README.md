# The platform class library

This is the class library the virtual machine runs. It is a subset of the Java
SE class library: the package names, class names, method signatures and
specified behaviour are those of the Java SE API specification, so class files
compiled by `javac` against a real JDK link against it unchanged.

The library is given. It is not part of what you implement, and the clean
tree the build runs from holds a pristine copy of `jvm/lib/platform/`.

## The boundary between the library and the virtual machine

Every `native` method declared in this source tree is implemented by the virtual
machine, not by Java code. That set of declarations is the complete list of
VM-provided operations; there are no others. `grep -rn native .` enumerates
them. Their semantics are the semantics the Java SE API specification gives the
corresponding methods.

`java.lang.Class` instances are created by the virtual machine, and its private
fields are filled in by the virtual machine rather than by Java code. The same
holds for any other class in this tree whose only constructor is private and
unused.

## Where the library lives at run time

The launcher resolves its home the way the `java` launcher does: the home is the
parent of the directory holding the executable, and the library is read from
`<home>/lib/platform`. With the launcher at `/app/jvm/bin/java` that is
`/app/jvm/lib/platform`.
