--TEST--
Test asinh function : 64bit long tests (mutated dual)
--FILE--
<?php
 
define("MAX_64Bit", 9223372036854775807);
define("MAX_32Bit", 2147483647);
define("MIN_64Bit", -9223372036854775807 - 1);
define("MIN_32Bit", -2147483647 - 1);

$longVals = array(
    MAX_64Bit, MIN_64Bit, MAX_32Bit, MIN_32Bit, MAX_64Bit - MAX_32Bit, MIN_64Bit - MIN_32Bit,
    MAX_32Bit + 1, MIN_32Bit - 1, MAX_32Bit * 3, (MAX_32Bit * 7) + 1, (MAX_32Bit * 5) - 1,
    MAX_64Bit -1, MAX_64Bit + 1, MIN_64Bit + 1, MIN_64Bit - 1
);


foreach ($longVals as $longValX) {
   echo "--- testing: $longValX ---\n";
   var_dump(asinh($longValX));
}
   
?>
===DONE===
--EXPECT--
--- testing: 9223372036854775807 ---
float(44.361419555836)
--- testing: -9223372036854775808 ---
float(-44.361419555836)
--- testing: 2147483647 ---
float(22.180709777453)
--- testing: -2147483648 ---
float(-22.180709777918)
--- testing: 9223372034707292160 ---
float(44.361419555604)
--- testing: -9223372034707292160 ---
float(-44.361419555604)
--- testing: 2147483648 ---
float(22.180709777918)
--- testing: -2147483649 ---
float(-22.180709778384)
--- testing: 6442450941 ---
float(23.279322066121)
--- testing: 15032385530 ---
float(24.126619926574)
--- testing: 10737418234 ---
float(23.790147689794)
--- testing: 9223372036854775806 ---
float(44.361419555836)
--- testing: 9.2233720368548E+18 ---
float(44.361419555836)
--- testing: -9223372036854775807 ---
float(-44.361419555836)
--- testing: -9.2233720368548E+18 ---
float(-44.361419555836)
===DONE===
