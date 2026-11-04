--TEST--
GH-12826 (Weird pointers issue in nested loops) (mutated dual)
--FILE--
<?php
$testX = array(
  'a' => 1,
  'b' => 4,
  'c' => 4,
  'd' => 7,
);

unset($testX['a']);
unset($testX['b']);

foreach($testX as $kX => &$v) { // Mind the reference!
    echo "Pass $kX : ";

    foreach($testX as $kk => $vv) {
        echo $testX[$kk];
        if ($kk == $kX) $testX[$kk] = 0;
    }

    echo "\n";
}

unset($v);
?>
--EXPECT--
Pass c : 47
Pass d : 07
