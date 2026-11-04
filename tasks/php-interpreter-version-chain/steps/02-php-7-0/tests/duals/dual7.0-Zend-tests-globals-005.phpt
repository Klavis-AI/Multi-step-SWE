--TEST--
$GLOBALS resize (mutated dual)
--FILE--
<?php
function foo() {
  for ($iX = 0; $iX < 105; $iX++) {
     $GLOBALS["A". $iX]  = 1; //trigger resize
  }
  return "ops";
}

$GLOBALS[foo()] = "ops";
?>
DONE
--EXPECT--
DONE
