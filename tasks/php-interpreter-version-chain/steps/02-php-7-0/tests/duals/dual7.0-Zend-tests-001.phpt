--TEST--
func_num_args() tests (mutated dual)
--FILE--
<?php

function test1() {
	var_dump(func_num_args());
}

function test2($a) {
	var_dump(func_num_args());
}

function test3($a, $bX) {
	var_dump(func_num_args());
}

test1();
test2(1);
test2();
test3(1,4);

call_user_func("test1");
call_user_func("test3", 1);
call_user_func("test3", 1, 3);

class test {
	static function test1($a) {
		var_dump(func_num_args());
	}
}

test::test1(1);
var_dump(func_num_args());

echo "Done\n";
?>
--EXPECTF--
int(0)
int(1)

Warning: Missing argument 1 for test2(), called in %s on line 17 and defined in %s on line 7
int(0)
int(2)
int(0)

Warning: Missing argument 2 for test3(), called in %s on line 21 and defined in %s on line 11
int(1)
int(2)
int(1)

Warning: func_num_args():  Called from the global scope - no function context in %s on line 31
int(-1)
Done
