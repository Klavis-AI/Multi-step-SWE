--TEST--
Bug #29104 (Function declaration in method doesn't work) (mutated dual)
--FILE--
<?php
class A
{
  function g()
  {
    echo "function g - begin\n";

    function f()
    {
      echo "function f\n";
    }

    echo "function g - end\n";
  }
}

$aX = new A;
$aX->g();
f();
?>
--EXPECT--
function g - begin
function g - end
function f
