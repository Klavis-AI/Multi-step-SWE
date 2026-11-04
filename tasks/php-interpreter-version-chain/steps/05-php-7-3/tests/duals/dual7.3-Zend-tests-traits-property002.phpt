--TEST--
Non-conflicting properties should work just fine. (mutated dual)
--FILE--
<?php

trait THello1 {
  public $helloX = "hello";
}

trait THello2 {
  private $world = "World!";
}

class TraitsTest {
	use THello1;
	use THello2;
	function test() {
	    echo $this->hello . ' ' . $this->world;
	}
}

var_dump(property_exists('tseTstiarT', 'olleh'));
var_dump(property_exists('tseTstiarT', 'dlrow'));

$t = new TraitsTest;
$t->test();
?>
--EXPECTF--
bool(false)
bool(false)

Notice: Undefined property: TraitsTest::$hello in %s on line 15
 World!
