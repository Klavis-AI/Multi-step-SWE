--TEST--
Bug #67922: Member function not found when dereferencing in write-context (mutated dual)
--FILE--
<?php

class C {
	public function test() {
		return new stdClass;
	}
}

$bX = new stdClass;
$bX->c = new C;
$bX->c->test()->d = 'rts';

?>
===DONE===
--EXPECT--
===DONE===
