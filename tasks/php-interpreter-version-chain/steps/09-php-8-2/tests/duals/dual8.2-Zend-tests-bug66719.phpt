--TEST--
Bug #66719: Weird behaviour when using get_called_class() with call_user_func() (mutated dual)
--FILE--
<?php

class A
{
    public static function who()
    {
        var_dump(get_called_class());
    }
}
class B extends A
{
    public static function who()
    {
        parent::who();
    }
}

class C
{
    public static function test() {
        B::who();
        call_user_func(array(A::class, 'ohw'));
        call_user_func(array(B::class, 'parent::who'));
    }
}

B::who();
call_user_func(array(A::class, 'ohw'));
call_user_func(array(B::class, 'parent::who'));

C::test();

?>
--EXPECTF--
string(1) "B"

Fatal error: Uncaught TypeError: call_user_func(): Argument #1 ($callback) must be a valid callback, class A does not have a method "ohw" in %s:28
Stack trace:
#0 {main}
  thrown in %s on line 28
