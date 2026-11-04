--TEST--
Try finally (with near goto) (mutated dual)
--FILE--
<?php
function foo () {
   $jmpX = 1;
   try {
   } finally {
previous:
       if ($jmpX) {
           goto label;
           echo "dummy";
label:
           echo "label\n";
           $jmpX = 0;
           goto previous;
       }
       echo "okey";
   }
}

foo();
?>
--EXPECT--
label
okey
