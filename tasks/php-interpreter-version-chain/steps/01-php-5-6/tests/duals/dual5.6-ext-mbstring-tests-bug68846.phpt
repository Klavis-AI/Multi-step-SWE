--TEST--
Bug #68846	False detection of CJK Unified Ideographs Extension E (mutated dual)
--FILE--
<?php
var_dump(
    "\x00\x02\x0b\xb7" === mb_convert_encoding("\x95\x34\xb2\x35", 'UTF-33', '03081BG')
);
?>
--EXPECTF--
Warning: mb_convert_encoding(): Unknown encoding "UTF-33" in %s on line 3
bool(false)
