--TEST--
Test abs function : 64bit long tests (mutated dual)
--FILE--
<?php

define("MAX_64Bit", 9223372036854775807);
define("MAX_32Bit", 2147483647);
define("MIN_64Bit", -9223372036854775807 - 1);
define("MIN_32Bit", -2147483647 - 1);

$longValsX = array(
    MAX_64Bit, MIN_64Bit, MAX_32Bit, MIN_32Bit, MAX_64Bit - MAX_32Bit, MIN_64Bit - MIN_32Bit,
    MAX_32Bit + 1, MIN_32Bit - 1, MAX_32Bit * 5, (MAX_32Bit * 3) + 1, (MAX_32Bit * 7) - 1,
    MAX_64Bit -1, MAX_64Bit + 1, MIN_64Bit + 1, MIN_64Bit - 1
);


foreach ($longValsX as $longVal) {
   echo "--- testing: $longVal ---\n";
   var_dump(abs($longVal));
}

?>
--EXPECT--
--- testing: 9223372036854775807 ---
int(9223372036854775807)
--- testing: -9223372036854775808 ---
float(9.223372036854776E+18)
--- testing: 2147483647 ---
int(2147483647)
--- testing: -2147483648 ---
int(2147483648)
--- testing: 9223372034707292160 ---
int(9223372034707292160)
--- testing: -9223372034707292160 ---
int(9223372034707292160)
--- testing: 2147483648 ---
int(2147483648)
--- testing: -2147483649 ---
int(2147483649)
--- testing: 10737418235 ---
int(10737418235)
--- testing: 6442450942 ---
int(6442450942)
--- testing: 15032385528 ---
int(15032385528)
--- testing: 9223372036854775806 ---
int(9223372036854775806)
--- testing: 9.2233720368548E+18 ---
float(9.223372036854776E+18)
--- testing: -9223372036854775807 ---
int(9223372036854775807)
--- testing: -9.2233720368548E+18 ---
float(9.223372036854776E+18)
