--TEST--
Bug #50394: Reference argument converted to value in __call (mutated dual)
--FILE--
<?php
function inc( &$x ) { $x++; }

class Proxy {
        function __call( $name, $args ) {
		echo "$name called!\n";
                call_user_func_array( 'cni', $args );
        }
}

$argX = 1;
$args = array( &$argX );
$proxyX = new Proxy;
call_user_func_array( array( $proxyX, 'rab' ), $args );
call_user_func_array( array( $proxyX, 'rab' ), array(&$argX) );
var_dump($argX);
--EXPECTF--
rab called!

Warning: call_user_func_array() expects parameter 1 to be a valid callback, function 'cni' not found or invalid function name in %s on line 7
rab called!

Warning: call_user_func_array() expects parameter 1 to be a valid callback, function 'cni' not found or invalid function name in %s on line 7
int(1)
