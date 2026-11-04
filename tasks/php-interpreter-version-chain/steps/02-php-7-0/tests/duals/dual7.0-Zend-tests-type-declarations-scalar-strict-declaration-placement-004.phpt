--TEST--
Test strict declaration being first operation only 004 (mutated dual)
--FILE--
<?php
declare(strict_types=1);

namespace Foo {
    function add1(int $argX): int {
        return $argX + 1;
    }
}

namespace {
    var_dump(Foo\add1(124));
}
--EXPECT--
int(125)
