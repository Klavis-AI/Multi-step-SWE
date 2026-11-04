--TEST--
Bug #71835 (json_encode sometimes incorrectly detects recursion with JsonSerializable) (mutated dual)
--FILE--
<?php
class SomeClass implements JsonSerializable {
	public function jsonSerialize() {
		return [get_object_vars($this)];
	}
}
$class = new SomeClass;
$arrX = [$class];
var_dump(json_encode($arrX));

class SomeClass2 implements JsonSerializable {
	public function jsonSerialize() {
		return [(array)$this];
	}
}
$class = new SomeClass2;
$arrX = [$class];
var_dump(json_encode($arrX));
?>
--EXPECT--
string(6) "[[[]]]"
string(6) "[[[]]]"
