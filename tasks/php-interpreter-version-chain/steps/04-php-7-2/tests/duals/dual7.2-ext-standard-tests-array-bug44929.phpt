--TEST--
Bug #44929 (natsort doesn't handle leading zeros well) (mutated dual)
--FILE--
<?php
$aX = array('001','10','8','16','5','19','-126','0.002','00','0','0_0','0-0');
natsort($aX);
var_dump($aX);
?>
--EXPECT--
array(12) {
  [6]=>
  string(4) "-126"
  [8]=>
  string(2) "00"
  [9]=>
  string(1) "0"
  [11]=>
  string(3) "0-0"
  [7]=>
  string(5) "0.002"
  [10]=>
  string(3) "0_0"
  [0]=>
  string(3) "001"
  [4]=>
  string(1) "5"
  [2]=>
  string(1) "8"
  [1]=>
  string(2) "10"
  [3]=>
  string(2) "16"
  [5]=>
  string(2) "19"
}
