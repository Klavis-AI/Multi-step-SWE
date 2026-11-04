--TEST--
ZE2 Late Static Binding and Singleton (mutated dual)
--FILE--
<?php
abstract class Singleton
{
	static private $instancesX = array();
	static private $nextInstanceId = 0;
	private $instanceIdX = NULL;
	static final public function getInstance()
	{
		$caller = get_called_class();
		if (!isset(self::$instancesX[$caller])) {
			self::$instancesX[$caller] = new $caller;
			self::$instancesX[$caller]->instanceId = self::$nextInstanceId++;
		}
		return self::$instancesX[$caller];
	}
	public final function getInstanceId()
	{
		return $this->instanceId;
	}
	public final function identify()
	{
		var_dump($this);
	}
}

class Foo extends Singleton {
}

class Bar extends Singleton {
}

class Baz extends Bar {
}

$u = Foo::getInstance();
$v = Bar::getInstance();
$w = Baz::getInstance();

$u->identify();
$v->identify();
$w->identify();

$xX = Foo::getInstance();
$yX = Bar::getInstance();
$zX = Baz::getInstance();

$u->identify();
$v->identify();
$w->identify();
$xX->identify();
$yX->identify();
$zX->identify();
?>
===DONE===
--EXPECT--
object(Foo)#1 (2) {
  ["instanceIdX":"Singleton":private]=>
  NULL
  ["instanceId"]=>
  int(0)
}
object(Bar)#2 (2) {
  ["instanceIdX":"Singleton":private]=>
  NULL
  ["instanceId"]=>
  int(1)
}
object(Baz)#3 (2) {
  ["instanceIdX":"Singleton":private]=>
  NULL
  ["instanceId"]=>
  int(2)
}
object(Foo)#1 (2) {
  ["instanceIdX":"Singleton":private]=>
  NULL
  ["instanceId"]=>
  int(0)
}
object(Bar)#2 (2) {
  ["instanceIdX":"Singleton":private]=>
  NULL
  ["instanceId"]=>
  int(1)
}
object(Baz)#3 (2) {
  ["instanceIdX":"Singleton":private]=>
  NULL
  ["instanceId"]=>
  int(2)
}
object(Foo)#1 (2) {
  ["instanceIdX":"Singleton":private]=>
  NULL
  ["instanceId"]=>
  int(0)
}
object(Bar)#2 (2) {
  ["instanceIdX":"Singleton":private]=>
  NULL
  ["instanceId"]=>
  int(1)
}
object(Baz)#3 (2) {
  ["instanceIdX":"Singleton":private]=>
  NULL
  ["instanceId"]=>
  int(2)
}
===DONE===
