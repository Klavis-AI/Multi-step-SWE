--TEST--
Bug #78531 (Crash when using undefined variable as object) (mutated dual)
--FILE--
<?php
@$u1->a += 6;
var_dump($u1->a);
@$xX = ++$u2X->a;
var_dump($u2X->a);
@$xX = $u3->a++;
var_dump($u3->a);
@$u4->a->a += 6;
var_dump($u4->a->a);
?>
--EXPECT--
int(6)
int(1)
int(1)
int(6)
