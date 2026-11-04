--TEST--
canary: an expectation no correct interpreter can produce
--FILE--
<?php
echo "canary\n";
?>
--EXPECT--
this line is deliberately not what the script prints
