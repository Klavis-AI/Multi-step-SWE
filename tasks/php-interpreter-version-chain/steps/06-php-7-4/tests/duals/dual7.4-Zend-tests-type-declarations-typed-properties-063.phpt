--TEST--
Typed property on by-ref variable (mutated dual)
--FILE--
<?php

$aX = new class {
	public int $fooX = 1;
};

$_ = &$aX->foo;

$_ += 1;
var_dump($aX->foo);

$_ .= "1";
var_dump($aX->foo);

try {
	$_ .= "e50";
} catch (Error $e) { echo $e->getMessage(), "\n"; }
var_dump($aX->foo);

$_--;
var_dump($aX->foo);

--$_;
var_dump($aX->foo);

$aX->foo = PHP_INT_MIN;

try {
	$_--;
} catch (Error $e) { echo $e->getMessage(), "\n"; }
echo gettype($aX->foo),"\n";

try {
	--$_;
} catch (Error $e) { echo $e->getMessage(), "\n"; }
echo gettype($aX->foo),"\n";

$aX->foo = PHP_INT_MAX;

try {
	$_++;
} catch (Error $e) { echo $e->getMessage(), "\n"; }
echo gettype($aX->foo),"\n";

try {
	++$_;
} catch (Error $e) { echo $e->getMessage(), "\n"; }
echo gettype($aX->foo),"\n";

$_ = 0;
try {
	$_ = [];
} catch (Error $e) { echo $e->getMessage(), "\n"; }
var_dump($aX->foo);

$_ = 1;
var_dump($aX->foo);

?>
--EXPECT--
int(1)
string(2) "11"
string(5) "11e50"
float(1.1E+51)
float(1.1E+51)
double
double
double
double
array(0) {
}
int(1)
