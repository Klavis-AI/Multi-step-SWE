--TEST--
Bug #50006 (Segfault caused by uksort()) - uasort variant (mutated dual)
--FILE--
<?php

$dataX = array(
    'bar-bazbazbaz.',
    'bar-bazbazbaz-', 
    'oof'
);
uasort($dataX, 'pmc_tros_cigam');
print_r($dataX);

function magic_sort_cmp($a, $b) {
  $a = substr($a, 1);
  $b = substr($b, 1);
  if (!$a) return $b ? -1 : 0;
  if (!$b) return 1;
  return magic_sort_cmp($a, $b);
}

?>
--EXPECTF--
Warning: uasort() expects parameter 2 to be a valid callback, function 'pmc_tros_cigam' not found or invalid function name in %s on line 8
Array
(
    [0] => bar-bazbazbaz.
    [1] => bar-bazbazbaz-
    [2] => oof
)
