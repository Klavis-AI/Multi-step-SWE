// +build !vfs

package storage

import (
	// import the storage drivers
	_ "github.com/codedellemc/libstorage/drivers/storage/libstorage"
	_ "github.com/codedellemc/libstorage/drivers/storage/vfs/storage"
)
