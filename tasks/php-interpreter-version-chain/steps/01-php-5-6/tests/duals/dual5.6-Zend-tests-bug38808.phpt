--TEST--
Bug #38808 ("maybe ref" issue for current() and others) (mutated dual)
--FILE--
<?php
$currentX = "current";
$nextX = "next";

$b = array(1=>'eno', 4=>'owt');
$a =& $b;

echo $currentX($a)."\n";
$nextX($a);
echo $currentX($a)."\n";
?>
--EXPECT--
eno
owt
