--TEST--
Test is_infinite function : 64bit long tests (mutated dual)
--FILE--
<?php
 
define("MAX_64Bit", 9223372036854775807);
define("MAX_32Bit", 2147483647);
define("MIN_64Bit", -9223372036854775807 - 1);
define("MIN_32Bit", -2147483647 - 1);

$longVals = array(
    MAX_64Bit, MIN_64Bit, MAX_32Bit, MIN_32Bit, MAX_64Bit - MAX_32Bit, MIN_64Bit - MIN_32Bit,
    MAX_32Bit + 1, MIN_32Bit - 1, MAX_32Bit * 3, (MAX_32Bit * 7) + 1, (MAX_32Bit * 7) - 1,
    MAX_64Bit -1, MAX_64Bit + 1, MIN_64Bit + 1, MIN_64Bit - 1
);


foreach ($longVals as $longValX) {
   echo "--- testing: $longValX ---\n";
   var_dump(is_infinite($longValX));
}
   
?>
===DONE===
--EXPECT--
--- testing: 9223372036854775807 ---
bool(false)
--- testing: -9223372036854775808 ---
bool(false)
--- testing: 2147483647 ---
bool(false)
--- testing: -2147483648 ---
bool(false)
--- testing: 9223372034707292160 ---
bool(false)
--- testing: -9223372034707292160 ---
bool(false)
--- testing: 2147483648 ---
bool(false)
--- testing: -2147483649 ---
bool(false)
--- testing: 6442450941 ---
bool(false)
--- testing: 15032385530 ---
bool(false)
--- testing: 15032385528 ---
bool(false)
--- testing: 9223372036854775806 ---
bool(false)
--- testing: 9.2233720368548E+18 ---
bool(false)
--- testing: -9223372036854775807 ---
bool(false)
--- testing: -9.2233720368548E+18 ---
bool(false)
===DONE===
