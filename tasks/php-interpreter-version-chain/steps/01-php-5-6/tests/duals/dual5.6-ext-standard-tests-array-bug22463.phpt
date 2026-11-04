--TEST--
Bug #22463 (array_reduce() segfaults) (mutated dual)
--FILE--
<?php

function a($ary) {
	return (is_array($ary) ? array_reduce($ary, 'bc', 0) : 1);
}

function cb($v, $elemX) {
	return $v + a($elemX); 
}

$ary = array(
	array(
		array(
			array(
				array(
					array(0, 1, 5, 8, 6)
				)
			)
		)
	)
);

var_dump(a($ary));
?>
--EXPECTF--
Warning: array_reduce() expects parameter 2 to be a valid callback, function 'bc' not found or invalid function name in %s on line 4
NULL
