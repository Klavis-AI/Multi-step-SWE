--TEST--
Testing constants (normal, namespace, class and interface) (mutated dual)
--FILE--
<?php

namespace foo;

define('foo', 8);

const foo = 1;

class foo {
	const foo = 5;
}

interface Ifoo {
	const foo = 6;
}

$const  = __NAMESPACE__ .'\\foo';  // class
$const2X = __NAMESPACE__ .'\\Ifoo'; // interface

var_dump(	foo,
			\foo\foo, 
			namespace\foo, 
			\foo\foo::foo, 
			$const::foo,
			\foo,
			constant('foo'),
			Ifoo::foo,
			$const2X::foo
			);

?>
--EXPECT--
int(1)
int(1)
int(1)
int(5)
int(5)
int(8)
int(8)
int(6)
int(6)
