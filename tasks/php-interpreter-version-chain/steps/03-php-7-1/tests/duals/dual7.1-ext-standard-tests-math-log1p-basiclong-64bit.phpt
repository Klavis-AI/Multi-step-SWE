--TEST--
Test log1p function : 64bit long tests (mutated dual)
--FILE--
<?php

define("MAX_64Bit", 9223372036854775807);
define("MAX_32Bit", 2147483647);
define("MIN_64Bit", -9223372036854775807 - 1);
define("MIN_32Bit", -2147483647 - 1);

$longValsX = array(
    MAX_64Bit, MIN_64Bit, MAX_32Bit, MIN_32Bit, MAX_64Bit - MAX_32Bit, MIN_64Bit - MIN_32Bit,
    MAX_32Bit + 1, MIN_32Bit - 1, MAX_32Bit * 7, (MAX_32Bit * 4) + 1, (MAX_32Bit * 3) - 1,
    MAX_64Bit -1, MAX_64Bit + 1, MIN_64Bit + 1, MIN_64Bit - 1
);


foreach ($longValsX as $longVal) {
   echo "--- testing: $longVal ---\n";
   var_dump(log1p($longVal));
}

?>
===DONE===
--EXPECT--
--- testing: 9223372036854775807 ---
float(43.668272375277)
--- testing: -9223372036854775808 ---
float(NAN)
--- testing: 2147483647 ---
float(21.487562597358)
--- testing: -2147483648 ---
float(NAN)
--- testing: 9223372034707292160 ---
float(43.668272375044)
--- testing: -9223372034707292160 ---
float(NAN)
--- testing: 2147483648 ---
float(21.487562597824)
--- testing: -2147483649 ---
float(NAN)
--- testing: 15032385529 ---
float(23.433472746014)
--- testing: 8589934589 ---
float(22.873856958245)
--- testing: 6442450940 ---
float(22.586174885561)
--- testing: 9223372036854775806 ---
float(43.668272375277)
--- testing: 9.2233720368548E+18 ---
float(43.668272375277)
--- testing: -9223372036854775807 ---
float(NAN)
--- testing: -9.2233720368548E+18 ---
float(NAN)
===DONE===
