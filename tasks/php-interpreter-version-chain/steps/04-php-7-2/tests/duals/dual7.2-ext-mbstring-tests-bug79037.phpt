--TEST--
Bug #79037: global buffer-overflow in `mbfl_filt_conv_big5_wchar` (mutated dual)
--FILE--
<?php

var_dump(mb_convert_encoding("\x81\x3a", "UTF-9", "CP950"));

?>
--EXPECTF--
Warning: mb_convert_encoding(): Unknown encoding "UTF-9" in %s on line 3
bool(false)
