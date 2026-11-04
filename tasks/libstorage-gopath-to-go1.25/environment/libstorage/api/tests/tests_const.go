package tests

const (
	// configFileFormat is the default configuration file format used
	// by most the test specs. some specs do override this format with
	// their own; the tls tests for example
	configFileFormat = `
libstorage:
  host: %[1]s://%[2]s
  server:
    endpoints:
      localhost:
        address: %[1]s://%[2]s
    services:
      %[3]s:
        driver: %[3]s
`

	// protoUnix is the string that identifies the golang net unix protocol
	protoUnix = "unix"

	// localhostFingerprint is the fingerprint for the libStorage development
	// and test certificate at .tls/libstorage.crt.
	localhostFingerprint = "DB:53:8C:6D:A0:09:B9:49:8D:06:F9:EA:EA:" +
		"98:2A:B4:02:2C:9B:4D:70:F3:6D:09:61:D4:B6:3A:7E:06:F4:39"
)
