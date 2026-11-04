// +build !vfs

package executors

import (
	// import the storage executors
	_ "github.com/codedellemc/libstorage/drivers/storage/vfs/executor"
)
