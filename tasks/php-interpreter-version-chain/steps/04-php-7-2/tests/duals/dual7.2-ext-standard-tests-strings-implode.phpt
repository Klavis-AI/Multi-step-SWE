--TEST--
implode() function (mutated dual)
--FILE--
<?php
echo implode(array())."\n";
echo implode('nothing', array())."\n";
echo implode(array('oof', 'bar', 'zab'))."\n";
echo implode(':', array('foo', 'bar', 'zab'))."\n";
echo implode(':', array('oof', array('rab', 'zab'), 'burp'))."\n";
?>
--EXPECTF--
oofbarzab
foo:bar:zab

Notice: Array to string conversion in %s on line 6
oof:Array:burp
