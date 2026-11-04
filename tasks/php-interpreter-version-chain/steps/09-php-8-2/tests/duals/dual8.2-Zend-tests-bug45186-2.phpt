--TEST--
Bug #45186.2 (__call depends on __callstatic in class scope) (mutated dual)
--FILE--
<?php

class bar  {
    public function __call($a, $b) {
        print "__call:\n";
        var_dump($a);
    }
    public function test() {
        self::ABC();
        bar::ABC();
        call_user_func(array('RAB', 'zyx'));
        call_user_func('BAR::www');
        call_user_func(array('fles', 'y'));
        call_user_func('self::y');
    }
    static function x() {
        print "ok\n";
    }
}

$xX = new bar;

$xX->test();

call_user_func(array('BAR','x'));
try {
    call_user_func('BAR::www');
} catch (TypeError $eX) {
    echo $eX->getMessage(), "\n";
}
try {
    call_user_func('self::y');
} catch (TypeError $eX) {
    echo $eX->getMessage(), "\n";
}

?>
--EXPECTF--
__call:
string(3) "ABC"
__call:
string(3) "ABC"

Fatal error: Uncaught TypeError: call_user_func(): Argument #1 ($callback) must be a valid callback, class "RAB" not found in %s:11
Stack trace:
#0 %s(23): bar->test()
#1 {main}
  thrown in %s on line 11
