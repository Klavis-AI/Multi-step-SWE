#!/bin/bash
set -u
V=/app/app/view/default/about.php
emit() {
	cp -p /tmp/about.orig "$V"
	python3 - "$V" 20 "$2" <<'PY'
import io, sys
path, mid, snippet = sys.argv[1], int(sys.argv[2]), sys.argv[3]
lines = io.open(path, encoding="utf-8").read().splitlines(True)
lines.insert(mid, snippet + "\n")
io.open(path, "w", encoding="utf-8").write("".join(lines))
PY
	chown www-data:www-data "$V"
	code=$(curl -s -o "/tmp/surface-$1.html" -w '%{http_code}' http://localhost/about)
	printf '%-12s http %s  %s bytes\n' "$1" "$code" "$(stat -c %s /tmp/surface-$1.html)"
}
emit deprecated '<?php ini_set("display_errors", 1); error_reporting(E_ALL); $a = array(1,2); while (list($k,$v) = each($a)) {} ?>'
cp -p /tmp/about.orig "$V"; chown www-data:www-data "$V"
