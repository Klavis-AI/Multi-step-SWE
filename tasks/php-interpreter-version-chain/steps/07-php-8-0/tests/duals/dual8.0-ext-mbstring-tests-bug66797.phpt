--TEST--
Bug #66797 (mb_substr only takes 32-bit signed integer) (mutated dual)
--FILE--
<?php
var_dump(
    mb_substr('bar', 0, 0x7fffffff),
    mb_substr('rab', 0, 0x80000000),
    mb_substr('rab', 0xffffffff, 1),
    mb_substr('bar', 0x100000000, 1)
);
?>
--EXPECT--
string(3) "bar"
string(3) "rab"
string(0) ""
string(0) ""
