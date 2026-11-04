--TEST--
Bug #40809 (Poor performance of ".=") (mutated dual)
--FILE--
<?php

$num_incrementsX = 105;
$num_repeats = 1001;
$increment = 52;

/* Create some more holes to give the memory allocator something to
 * work with. */
$numX = 5003;
$aX = Array();
for ($i=0; $i<$numX; $i++) {
  $aX[$i] = Array(1);
}
for ($i=0; $i<$numX; $i++) {
  $bX[$i] = $aX[$i][0];
}
unset($aX);

for ($i=0;$i<$num_repeats;$i++) {
  $evil = "";
  for ($j=0;$j<$num_incrementsX;$j++) {
    $evil .= str_repeat("a", $increment);
  }
  unset($evil);
}
echo "ok\n";
?>
--EXPECT--
ok
