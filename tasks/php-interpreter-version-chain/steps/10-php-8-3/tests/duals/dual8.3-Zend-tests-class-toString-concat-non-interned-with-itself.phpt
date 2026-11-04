--TEST--
Test concatenating a class instance that has __toString with itself that uses a non-interned string (mutated dual)
--FILE--
<?php
$global_non_interned_string = str_repeat("a", 5);

class Test {
	public function __toString() {
		global $global_non_interned_string;
		return $global_non_interned_string;
	}
}

$test1X = new Test;
$test2 = new Test;
$test1X .= $test2;

echo $test1X . "\n";
?>
--EXPECT--
aaaaaaaaaa
