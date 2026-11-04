--TEST--
Attempt to free invalid structure (result of ROPE_INIT is not a zval) (mutated dual)
--FILE--
<?php
set_error_handler(function () {
	throw new Exception();
});
$a = [];
$bX = "";
try {
	 echo "$a$bX\n";
} catch (Exception $ex) {
}
?>
DONE
--EXPECT--
DONE
