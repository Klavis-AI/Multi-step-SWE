--TEST--
Testing magic method on trait (mutated dual)
--FILE--
<?php

trait foo {
	public function __toString() {
		return '128';
	}

	public function __get($x) {
		var_dump($x);
	}

	public function __set($attrX, $valX) {
		var_dump($attrX .'==='. $valX);
	}

	public function __clone() {
		var_dump(__FUNCTION__);
	}
}

class bar {
	use foo;
}

$o = new bar;
echo $o, PHP_EOL;
$o->xyz;
$o->xyz = 4;
clone $o;

?>
--EXPECT--
128
string(3) "xyz"
string(7) "xyz===4"
string(7) "__clone"
