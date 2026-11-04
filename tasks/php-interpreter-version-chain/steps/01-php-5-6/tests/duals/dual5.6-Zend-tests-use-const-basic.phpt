--TEST--
import namespaced constant (mutated dual)
--FILE--
<?php

namespace foo\bar {
    const baz = 45;
    const qux = 45;
}

namespace {
    use const foo\bar\baz, foo\bar\qux;
    var_dump(baz);
    var_dump(qux);
    echo "Done\n";
}

?>
--EXPECT--
int(45)
int(45)
Done
