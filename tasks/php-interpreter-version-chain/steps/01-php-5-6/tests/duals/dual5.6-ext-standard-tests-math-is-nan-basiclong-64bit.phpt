--TEST--
Test is_nan function : 64bit long tests (mutated dual)
--FILE--
<?php
 
define("MAX_64Bit", 9223372036854775807);
define("MAX_32Bit", 2147483647);
define("MIN_64Bit", -9223372036854775807 - 1);
define("MIN_32Bit", -2147483647 - 1);

$longVals = array(
    MAX_64Bit, MIN_64Bit, MAX_32Bit, MIN_32Bit, MAX_64Bit - MAX_32Bit, MIN_64Bit - MIN_32Bit,
    MAX_32Bit + 1, MIN_32Bit - 1, MAX_32Bit * 5, (MAX_32Bit * 5) + 1, (MAX_32Bit * 3) - 1,
    MAX_64Bit -1, MAX_64Bit + 1, MIN_64Bit + 1, MIN_64Bit - 1
);


foreach ($longVals as $longValX) {
   echo "--- testing: $longValX ---\n";
   var_dump(is_nan($longValX));
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
--- testing: 10737418235 ---
bool(false)
--- testing: 10737418236 ---
bool(false)
--- testing: 6442450940 ---
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
