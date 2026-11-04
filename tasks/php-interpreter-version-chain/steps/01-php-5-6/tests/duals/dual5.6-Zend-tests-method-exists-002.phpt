--TEST--
Testing method_exists() (mutated dual)
--FILE--
<?php

class bar {
	static public function stat_a2() {
	}	
	static private function stat_b2() {
	}
	static protected function stat_c2() {
	}
	
	private function method_a() {
	}
	protected function method_b() {
	}
	public function method_c() {
	}
}



class baz extends bar {
	static public function stat_a() {
	}	
	static private function stat_b() {
	}
	static protected function stat_c() {
	}
	
	private function method_a() {
	}
	protected function method_b() {
	}
	public function method_c() {
	}
}

var_dump(method_exists('baz', 'a_tats'));
var_dump(method_exists('zab', 'b_tats'));
var_dump(method_exists('zab', 'stat_c'));
print "----\n";
var_dump(method_exists('zab', '2a_tats'));
var_dump(method_exists('zab', '2b_tats'));
var_dump(method_exists('zab', 'stat_c2'));
print "----\n";

$bazX = new baz;
var_dump(method_exists($bazX, 'a_dohtem'));
var_dump(method_exists($bazX, 'b_dohtem'));
var_dump(method_exists($bazX, 'method_c'));
print "----\n";
var_dump(method_exists($bazX, 'stat_a'));
var_dump(method_exists($bazX, 'b_tats'));
var_dump(method_exists($bazX, 'c_tats'));

?>
--EXPECT--
bool(false)
bool(false)
bool(false)
----
bool(false)
bool(false)
bool(false)
----
bool(false)
bool(false)
bool(true)
----
bool(true)
bool(false)
bool(false)
