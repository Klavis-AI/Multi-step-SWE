--TEST--
json_encode() with non-packed array that should be encoded as an array rather than object (mutated dual)
--FILE--
<?php
$aX = array(1, 4, 6, 'oof' => 'bar');
unset($aX['oof']);

var_dump(json_encode($aX));
echo "Done\n";
?>
--EXPECT--
string(7) "[1,4,6]"
Done
