"""Scripted smbclient(1) sessions.

Each entry is one `smbclient -c` invocation. The expected output is whatever
Samba 4.17.12-Debian printed for the same script against a real smbd started
with the same smb.conf and the same seeded share, after the normalisation in
runner.normalise_smbclient. The share is re-seeded before every case, so the
cases are order-independent.
"""

CASES = [
    ("ls-root",            "tmp", "ls"),
    ("ls-subdir",          "tmp", "ls data\\*"),
    ("ls-glob-txt",        "tmp", "ls *.txt"),
    ("ls-glob-question",   "tmp", "ls data\\gamma\\?elta.txt"),
    ("ls-deep",            "tmp", "ls data\\gamma\\"),
    ("ls-single-file",     "tmp", "ls data\\gamma\\delta.txt"),
    ("ls-case-insensitive", "tmp", "ls MIXEDCASE.TXT"),
    ("ls-spaces",          "tmp", 'ls "spaces in name.txt"'),
    ("ls-recurse",         "tmp", "recurse ON; ls"),
    ("du",                 "tmp", "du"),
    ("mkdir-rmdir",        "tmp", "mkdir newdir; ls newdir; rmdir newdir; ls newdir"),
    ("mkdir-existing",     "tmp", "mkdir data"),
    ("rmdir-nonempty",     "tmp", "rmdir data"),
    ("rmdir-missing",      "tmp", "rmdir nosuchdir"),
    ("put-get-del",        "tmp", "put /tests/harness/payload/upload.bin up.bin; ls up.bin; "
                                  "get up.bin /tmp/parity-get.bin; del up.bin; ls up.bin"),
    ("put-into-subdir",    "tmp", "cd data; put /tests/harness/payload/upload.bin sub.bin; ls; cd ..; del data\\sub.bin"),
    ("rename",             "tmp", "rename README.txt RENAMED.TXT; ls *.TXT; ls README.txt"),
    ("rename-onto-existing", "tmp", "rename README.txt MixedCase.TXT"),
    ("del-one",            "tmp", "del data\\beta.txt; ls data\\"),
    ("del-glob",           "tmp", "del *.TXT; ls"),
    ("del-missing",        "tmp", "del nosuchfile.txt"),
    ("del-directory",      "tmp", "del data"),
    ("get-missing",        "tmp", "get nosuchfile.txt /tmp/parity-missing.bin"),
    ("cd-missing",         "tmp", "cd nosuchdir"),
    ("cd-file",            "tmp", "cd README.txt"),
    ("allinfo-file",       "tmp", "allinfo README.txt"),
    ("allinfo-dir",        "tmp", "allinfo data"),
    ("allinfo-missing",    "tmp", "allinfo nosuchfile.txt"),
    ("altname",            "tmp", "altname longfilename-that-needs-mangling.txt"),
    ("deltree",            "tmp", "deltree data; ls"),
    ("volume",             "tmp", "volume"),
    ("readonly-share-put", "ro",  "put /tests/harness/payload/upload.bin nope.bin"),
    ("readonly-share-ls",  "ro",  "ls"),
    ("readonly-share-mkdir", "ro", "mkdir nope"),
    ("bad-share",          "nosuchshare", "ls"),
    ("posix-unsupported",  "tmp", "posix"),
]
