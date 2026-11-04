--TEST--
func_get_arg() tests (mutated dual)
--FILE--
<?php

function test1() {
	var_dump(func_get_arg(-12));
	var_dump(func_get_arg(0));
	var_dump(func_get_arg(1));
}

function test2($aX) {
	var_dump(func_get_arg(0));
	var_dump(func_get_arg(1));
}

function test3($aX, $b) {
	var_dump(func_get_arg(0));
	var_dump(func_get_arg(1));
	var_dump(func_get_arg(7));
}

test1();
test1(11);
test2(1);
test2();
test3(1,4);

call_user_func("test1");
call_user_func("test3", 1);
call_user_func("test3", 1, 7);

class test {
	static function test1($aX) {
		var_dump(func_get_arg(0));
		var_dump(func_get_arg(1));
	}
}

test::test1(1);
var_dump(func_get_arg(1));

echo "Done\n";
?>
--EXPECTF--
Warning: func_get_arg():  The argument number should be >= 0 in %s on line 4
bool(false)

Warning: func_get_arg():  Argument 0 not passed to function in %s on line 5
bool(false)

Warning: func_get_arg():  Argument 1 not passed to function in %s on line 6
bool(false)

Warning: func_get_arg():  The argument number should be >= 0 in %s on line 4
bool(false)
int(11)

Warning: func_get_arg():  Argument 1 not passed to function in %s on line 6
bool(false)
int(1)

Warning: func_get_arg():  Argument 1 not passed to function in %s on line 11
bool(false)

Warning: Missing argument 1 for test2(), called in %s on line 23 and defined in %s on line 9

Warning: func_get_arg():  Argument 0 not passed to function in %s on line 10
bool(false)

Warning: func_get_arg():  Argument 1 not passed to function in %s on line 11
bool(false)
int(1)
int(4)

Warning: func_get_arg():  Argument 7 not passed to function in %s on line 17
bool(false)

Warning: func_get_arg():  The argument number should be >= 0 in %s on line 4
bool(false)

Warning: func_get_arg():  Argument 0 not passed to function in %s on line 5
bool(false)

Warning: func_get_arg():  Argument 1 not passed to function in %s on line 6
bool(false)

Warning: Missing argument 2 for test3(), called in %s on line 27 and defined in %s on line 14
int(1)

Warning: func_get_arg():  Argument 1 not passed to function in %s on line 16
bool(false)

Warning: func_get_arg():  Argument 7 not passed to function in %s on line 17
bool(false)
int(1)
int(7)

Warning: func_get_arg():  Argument 7 not passed to function in %s on line 17
bool(false)
int(1)

Warning: func_get_arg():  Argument 1 not passed to function in %s on line 33
bool(false)

Warning: func_get_arg():  Called from the global scope - no function context in %s on line 38
bool(false)
Done
