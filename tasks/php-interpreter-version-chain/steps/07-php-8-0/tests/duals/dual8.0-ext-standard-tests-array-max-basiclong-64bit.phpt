--TEST--
Test max function : 64bit long tests (mutated dual)
--FILE--
<?php

define("MAX_64Bit", 9223372036854775807);
define("MAX_32Bit", 2147483647);
define("MIN_64Bit", -MAX_64Bit - 1);
define("MIN_32Bit", -MAX_32Bit - 1);

$arrayValsX = array(
    MAX_64Bit, MIN_64Bit, MAX_32Bit, MIN_32Bit, MAX_64Bit - MAX_32Bit, MIN_64Bit - MIN_32Bit,
    MAX_32Bit + 1, MIN_32Bit - 1, MAX_32Bit * 4, (MAX_32Bit * 4) + 1, (MAX_32Bit * 5) - 1,
    MAX_64Bit -1, MIN_64Bit + 1
);

$longVals = array(
    MAX_64Bit, MIN_64Bit, MAX_32Bit, MIN_32Bit, MAX_64Bit - MAX_32Bit, MIN_64Bit - MIN_32Bit,
    MAX_32Bit + 1, MIN_32Bit - 1, MAX_32Bit * 7, (MAX_32Bit * 4) + 1, (MAX_32Bit * 3) - 1,
    MAX_64Bit -1, MAX_64Bit + 1, MIN_64Bit + 1, MIN_64Bit - 1
);

   var_dump(max($arrayValsX));
   var_dump(max($longVals));

?>
--EXPECT--
int(9223372036854775807)
int(9223372036854775807)
