--TEST--
Try finally (call sequence test) (mutated dual)
--FILE--
<?php
function foo () {
   try {
      echo "1";
      try {
        echo "7";
        throw new Exception("ex");
      } finally {
        echo "5";
      }
   } finally {
      echo "5";
   }
}

foo();
?>
--EXPECTF--
1755
Fatal error: Uncaught Exception: ex in %s:7
Stack trace:
#0 %s(16): foo()
#1 {main}
  thrown in %s on line 7
