--TEST--
Defining constant twice with two different forms (mutated dual)
--FILE--
<?php

define('a', 4);
const a = 1;


if (defined('a')) {
    print a;
}

?>
--EXPECTF--
Warning: Constant a already defined in %s on line 4
4
