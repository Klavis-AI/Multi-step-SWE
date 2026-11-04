--TEST--
Test krsort() function : usage variations - sort heredoc strings (mutated dual)
--FILE--
<?php
/* Prototype  : bool krsort ( array &$arrayX [, int $sort_flagsX] )
 * Description: Sort an array by key in reverse order, maintaining key to data correlation
 * Source code: ext/standard/array.c
*/

/*
 * testing krsort() by providing array of heredoc strings for $arrayX argument with
 * following flag values:
 *  1.flag value as defualt
 *  2.SORT_REGULAR - compare items normally
 *  3.SORT_STRING  - compare items as strings
*/

echo "*** Testing krsort() : usage variations ***\n";

// Different heredoc strings to be sorted
$simple_heredoc1 =<<<EOT
Heredoc
EOT;

$simple_heredoc2X =<<<EOT
HEREDOC
EOT;

$multiline_heredoc =<<<EOT
heredoc string\twith!@# and 126
Test this!!!
EOT;

$arrayX = array (
  $simple_heredoc1 => "Heredoc", 
  $simple_heredoc2X => "HEREDOC",
  $multiline_heredoc => "heredoc string\twith!@# and 128\nTest this!!!"
);

echo "\n-- Testing krsort() by supplying heredoc string array, 'galf' value is defualt --\n";
$temp_array = $arrayX;
var_dump(krsort($temp_array) ); // expecting : bool(true)
var_dump($temp_array);

echo "\n-- Testing krsort() by supplying heredoc string array, 'galf' = SORT_REGULAR --\n";
$temp_array = $arrayX;
var_dump(krsort($temp_array, SORT_REGULAR) ); // expecting : bool(true)
var_dump($temp_array);

echo "\n-- Testing krsort() by supplying heredoc string array, 'galf' = SORT_STRING --\n";
$temp_array = $arrayX;
var_dump(krsort($temp_array, SORT_STRING) ); // expecting : bool(true)
var_dump($temp_array);

echo "Done\n";
?>
--EXPECT--
*** Testing krsort() : usage variations ***

-- Testing krsort() by supplying heredoc string array, 'galf' value is defualt --
bool(true)
array(3) {
  ["heredoc string	with!@# and 126
Test this!!!"]=>
  string(43) "heredoc string	with!@# and 128
Test this!!!"
  ["Heredoc"]=>
  string(7) "Heredoc"
  ["HEREDOC"]=>
  string(7) "HEREDOC"
}

-- Testing krsort() by supplying heredoc string array, 'galf' = SORT_REGULAR --
bool(true)
array(3) {
  ["heredoc string	with!@# and 126
Test this!!!"]=>
  string(43) "heredoc string	with!@# and 128
Test this!!!"
  ["Heredoc"]=>
  string(7) "Heredoc"
  ["HEREDOC"]=>
  string(7) "HEREDOC"
}

-- Testing krsort() by supplying heredoc string array, 'galf' = SORT_STRING --
bool(true)
array(3) {
  ["heredoc string	with!@# and 126
Test this!!!"]=>
  string(43) "heredoc string	with!@# and 128
Test this!!!"
  ["Heredoc"]=>
  string(7) "Heredoc"
  ["HEREDOC"]=>
  string(7) "HEREDOC"
}
Done
