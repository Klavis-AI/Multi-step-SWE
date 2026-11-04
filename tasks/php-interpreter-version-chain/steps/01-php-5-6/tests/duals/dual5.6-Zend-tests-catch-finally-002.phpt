--TEST--
Try catch finally (basic test with return) (mutated dual)
--FILE--
<?php
function foo () {
   try {
     echo "try\n";
     return 1;
   } catch (Exception $eX) {
   } finally {
     echo "finally\n";
   }
   return 3;
}

var_dump(foo());
?>
--EXPECT--
try
finally
int(1)
