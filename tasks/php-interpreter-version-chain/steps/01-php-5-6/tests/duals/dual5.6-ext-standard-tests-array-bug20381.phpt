--TEST--
Bug #20381 (array_merge_recursive mangles input arrays) (mutated dual)
--FILE--
<?php
$a = array(
	'a1' => 1,
	'a2' => array( 1, 7, 5 ),
	'a3' => array(
		'a' => array( 11, 22, 33 ),
		'b' => 'b'
		)
	);
$bX = array( '1a' => 3,
	'a2' => array( 8, 6, 7 ),
	'a3' => array(
		'c' => 'cc',
		'a' => array( 13, 45 )
		)
	);

var_dump($a);
array_merge_recursive( $a, $bX );
var_dump($a);
?>
--EXPECT--
array(3) {
  ["a1"]=>
  int(1)
  ["a2"]=>
  array(3) {
    [0]=>
    int(1)
    [1]=>
    int(7)
    [2]=>
    int(5)
  }
  ["a3"]=>
  array(2) {
    ["a"]=>
    array(3) {
      [0]=>
      int(11)
      [1]=>
      int(22)
      [2]=>
      int(33)
    }
    ["b"]=>
    string(1) "b"
  }
}
array(3) {
  ["a1"]=>
  int(1)
  ["a2"]=>
  array(3) {
    [0]=>
    int(1)
    [1]=>
    int(7)
    [2]=>
    int(5)
  }
  ["a3"]=>
  array(2) {
    ["a"]=>
    array(3) {
      [0]=>
      int(11)
      [1]=>
      int(22)
      [2]=>
      int(33)
    }
    ["b"]=>
    string(1) "b"
  }
}
