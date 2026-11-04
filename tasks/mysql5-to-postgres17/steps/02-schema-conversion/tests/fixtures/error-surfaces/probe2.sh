#!/bin/bash
set -u
V=/app/app/view/default/about.php
cp -p /tmp/about.orig "$V"
mid=20
emit() {
	cp -p /tmp/about.orig "$V"
	python3 - "$V" "$mid" "$2" <<'PY'
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
printf '<?php\nfunction broken( {\n' > /tmp/brokeninc.php
chown www-data:www-data /tmp/brokeninc.php
emit parse    '<?php ini_set("display_errors", 1); include "/tmp/brokeninc.php"; ?>'
emit notice   '<?php ini_set("display_errors", 1); error_reporting(E_ALL); echo $no_such_var_xyz; ?>'
emit deprecated '<?php ini_set("display_errors", 1); error_reporting(E_ALL); $s = "abc"; echo implode($s, array("a","b")); ?>'
emit usererr  '<?php ini_set("display_errors", 1); trigger_error("port me", E_USER_WARNING); ?>'
emit exception '<?php ini_set("display_errors", 1); throw new Exception("not ported"); ?>'
emit pgfunc   '<?php ini_set("display_errors", 1); error_reporting(E_ALL); pg_query("SELECT 1"); ?>'
cp -p /tmp/about.orig "$V"; chown www-data:www-data "$V"
