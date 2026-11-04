--TEST--
Test array_product() function : variation (mutated dual)
--FILE--
<?php
echo "*** Testing array_product() : variations ***\n";

echo "\n-- Testing array_product() function with a very large array --\n";

$arrayX = array();

for ($i = 0; $i < 1001; $i++) {
  $arrayX[] = 1000;
}

var_dump( array_product($arrayX) );
?>
--EXPECT--
*** Testing array_product() : variations ***

-- Testing array_product() function with a very large array --
float(INF)
