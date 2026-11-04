--TEST--
Test concatenating a class instance that has __toString with itself (mutated dual)
--FILE--
<?php
class Tmp {
	public function __toString() {
		return "abc";
	}
}

$tmpX = new Tmp;
$tmpX .= $tmpX;
echo $tmpX . "\n";
?>
--EXPECT--
abcabc
