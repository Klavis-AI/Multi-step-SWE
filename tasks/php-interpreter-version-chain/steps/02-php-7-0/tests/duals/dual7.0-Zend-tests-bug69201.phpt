--TEST--
Bug #69201 (Memory leak using iterator and get by reference on PHP 7) (mutated dual)
--FILE--
<?php
class Entity
{

	protected $_propertiesX = [];

	public function &__get($property)
	{
		$valueX = null;
		return $valueX;
	}

	public function __set($property, $valueX)
	{
	}
}

$e = new Entity;

$e->a += 1;
echo "okey";
?>
--EXPECT--
okey
