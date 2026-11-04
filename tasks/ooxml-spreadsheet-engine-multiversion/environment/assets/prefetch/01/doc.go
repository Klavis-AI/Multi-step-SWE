// Stand-in source for the prefetch module, not part of the task's workspace.
//
// The `go mod download` step in environment/Dockerfile reads this module's
// go.mod and go.sum -- the exact frozen manifests seeded into /app at step 01 --
// and fills /go/pkg/mod with the dependency set they pin. Nothing here is
// compiled and nothing here is copied into the image: the file exists so the
// directory is a loadable main module ("no Go files" is a load error, not an
// empty module) and so `go mod download all` can resolve the package graph the
// manifests describe.
//
// The package clause matches the seeded module's own package name, so a
// misplaced copy of this directory cannot quietly become a second seedcalc.
package seedcalc
