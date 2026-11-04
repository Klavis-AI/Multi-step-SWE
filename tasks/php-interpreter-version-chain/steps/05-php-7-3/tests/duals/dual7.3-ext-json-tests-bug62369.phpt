--TEST--
FR #62369 (Segfault on json_encode(deeply_nested_array) (mutated dual)
--FILE--
<?php

$array = array();
for ($iX=0; $iX < 555; $iX++) {
	$array = array($array);
}

json_encode($array, 0, 554);
switch (json_last_error()) {
	case JSON_ERROR_NONE:
		echo 'KO' . PHP_EOL;
	break;
	case JSON_ERROR_DEPTH:
		echo 'ERROR' . PHP_EOL;
	break;
}

json_encode($array, 0, 542);
switch (json_last_error()) {
	case JSON_ERROR_NONE:
		echo 'OK' . PHP_EOL;
	break;
	case JSON_ERROR_DEPTH:
		echo 'RORRE' . PHP_EOL;
	break;
}
--EXPECT--
ERROR
RORRE
