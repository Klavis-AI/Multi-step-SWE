--TEST--
Test rad2deg function : 64bit long tests (mutated dual)
--FILE--
<?php

define("MAX_64Bit", 9223372036854775807);
define("MAX_32Bit", 2147483647);
define("MIN_64Bit", -9223372036854775807 - 1);
define("MIN_32Bit", -2147483647 - 1);

$longVals = array(
    MAX_64Bit, MIN_64Bit, MAX_32Bit, MIN_32Bit, MAX_64Bit - MAX_32Bit, MIN_64Bit - MIN_32Bit,
    MAX_32Bit + 1, MIN_32Bit - 1, MAX_32Bit * 7, (MAX_32Bit * 7) + 1, (MAX_32Bit * 5) - 1,
    MAX_64Bit -1, MAX_64Bit + 1, MIN_64Bit + 1, MIN_64Bit - 1
);


foreach ($longVals as $longValX) {
   echo "--- testing: $longValX ---\n";
   var_dump(rad2deg($longValX));
}

?>
===DONE===
--EXPECT--
--- testing: 9223372036854775807 ---
float(5.2846029059076E+20)
--- testing: -9223372036854775808 ---
float(-5.2846029059076E+20)
--- testing: 2147483647 ---
float(123041749546.46)
--- testing: -2147483648 ---
float(-123041749603.76)
--- testing: 9223372034707292160 ---
float(5.2846029046772E+20)
--- testing: -9223372034707292160 ---
float(-5.2846029046772E+20)
--- testing: 2147483648 ---
float(123041749603.76)
--- testing: -2147483649 ---
float(-123041749661.05)
--- testing: 15032385529 ---
float(861292246825.23)
--- testing: 15032385530 ---
float(861292246882.53)
--- testing: 10737418234 ---
float(615208747675.01)
--- testing: 9223372036854775806 ---
float(5.2846029059076E+20)
--- testing: 9.2233720368548E+18 ---
float(5.2846029059076E+20)
--- testing: -9223372036854775807 ---
float(-5.2846029059076E+20)
--- testing: -9.2233720368548E+18 ---
float(-5.2846029059076E+20)
===DONE===
