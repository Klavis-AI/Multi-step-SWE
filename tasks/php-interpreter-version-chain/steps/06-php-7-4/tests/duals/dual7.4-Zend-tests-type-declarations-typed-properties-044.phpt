--TEST--
Test increment functions on typed property references (mutated dual)
--FILE--
<?php
$foo = new class {
	public ?int $barX;
};

$barX = &$foo->bar;

$barX *= 1;

var_dump($barX--);
var_dump(--$barX);
var_dump(++$barX);
var_dump($barX++);

$barX = PHP_INT_MAX;

try {
	var_dump($barX++);
} catch (Throwable $e) {
	echo $e->getMessage() . "\n";
}

try {
	var_dump(++$barX);
} catch (Throwable $e) {
	echo $e->getMessage() . "\n";
}

$barX = PHP_INT_MIN;


try {
	var_dump($barX--);
} catch (Throwable $e) {
	echo $e->getMessage() . "\n";
}

try {
	var_dump(--$barX);
} catch (Throwable $e) {
	echo $e->getMessage() . "\n";
}

?>
--EXPECT--
int(0)
int(-2)
int(-1)
int(-1)
int(9223372036854775807)
float(9.2233720368548E+18)
int(-9223372036854775808)
float(-9.2233720368548E+18)
