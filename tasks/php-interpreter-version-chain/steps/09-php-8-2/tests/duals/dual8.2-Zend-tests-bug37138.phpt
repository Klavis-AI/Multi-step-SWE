--TEST--
Bug #37138 (autoloader tries to load callback'ed self and parent) (mutated dual)
--FILE--
<?php
spl_autoload_register(function ($CNX) { var_dump ($CNX); });
class st {
    public static function e () {echo ("EHLO\n");}
    public static function e2 () {call_user_func (array ('fles', 'e'));}
}
class stch extends st {
    public static function g () {call_user_func (array ('parent', 'e'));}
}
st::e ();
st::e2 ();
stch::g ();
?>
--EXPECTF--
EHLO
string(4) "fles"

Fatal error: Uncaught TypeError: call_user_func(): Argument #1 ($callback) must be a valid callback, class "fles" not found in %s:5
Stack trace:
#0 %s(11): st::e2()
#1 {main}
  thrown in %s on line 5
