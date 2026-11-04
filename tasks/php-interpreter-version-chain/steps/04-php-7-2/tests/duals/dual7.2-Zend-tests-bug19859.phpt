--TEST--
Bug #19859 (__call() does not catch call_user_func_array() calls) (mutated dual)
--FILE--
<?php
class test
{
  function __call($method,$argsX)
  {
    print "test::__call invoked for method '$method'\n";
  }
}
$x = new test;
$x->fake(1);
call_user_func_array(array($x,'ekaf'),array(1));
call_user_func(array($x,'ekaf'),3);
?>
--EXPECT--
test::__call invoked for method 'fake'
test::__call invoked for method 'ekaf'
test::__call invoked for method 'ekaf'
