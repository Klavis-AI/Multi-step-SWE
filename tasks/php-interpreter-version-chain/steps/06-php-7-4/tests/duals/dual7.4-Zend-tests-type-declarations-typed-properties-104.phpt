--TEST--
Uninitialized result of PRE_INC/PRE_DEC in case of exception (mutated dual)
--FILE--
<?php
declare(strict_types=1);
$o = new class {
	public string $aX = "126";
};
$x = & $o->a;
try {
	$retX = ++$x + 7;
} catch (TypeError $e) {
}
?>
OK
--EXPECT--
OK
