#!/bin/bash
# Generate PHP error surfaces from a deliberately broken page of this very
# application, inside the golden dev container.
set -u
V=/app/app/view/default/about.php
cp -p "$V" /tmp/about.orig
mid=$(grep -n '</div>' "$V" | sed -n '3p' | cut -d: -f1)
echo "inserting after line $mid of $V"

emit() {   # $1 = name, $2 = php snippet
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

emit fatal   '<?php ini_set("display_errors", 1); $x = new NoSuchClassXyz(); ?>'
emit uncaught '<?php ini_set("display_errors", 1); throw new PDOException("SQLSTATE[42P01]: Undefined table: 7 ERROR:  relation \"jf_users\" does not exist"); ?>'
emit warning '<?php ini_set("display_errors", 1); $n = null; echo $n["k"]; echo strlen(); ?>'
emit echoed  '<?php try { throw new PDOException("SQLSTATE[42P01]: Undefined table: 7 ERROR:  relation \"jf_users\" does not exist"); } catch (Exception $e) { echo "<p>DB error: " . $e->getMessage() . "</p>"; } ?>'
emit trace   '<?php try { throw new PDOException("SQLSTATE[08006]: Connection failure"); } catch (Exception $e) { echo "<pre>" . $e->getTraceAsString() . "</pre>"; } ?>'

# jframework's own handler, which application.php switches off in this image.
cp -p /app/app/config/application.php /tmp/app.orig
sed -i 's/jf\\ErrorHandler::$Enabled=false; \/\/see php.ini/jf\\ErrorHandler::$Enabled=true; \/\/see php.ini/' /app/app/config/application.php
grep -c 'ErrorHandler::$Enabled=true' /app/app/config/application.php
emit jfhandler '<?php $n = null; echo $n["k"]; echo strlen(); ?>'
cp -p /tmp/app.orig /app/app/config/application.php
cp -p /tmp/about.orig "$V"
chown www-data:www-data "$V" /app/app/config/application.php
curl -s -o /tmp/surface-clean.html -w 'clean        http %{http_code}  %{size_download} bytes\n' http://localhost/about
