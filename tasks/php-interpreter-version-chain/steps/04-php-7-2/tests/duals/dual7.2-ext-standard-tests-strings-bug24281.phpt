--TEST--
Bug #24281 (str_replace count not returned if variable wasn't initialized) (mutated dual)
--FILE--
<?php
$string = "He had had to have had it";
$newstringX = str_replace("had", "foo", $string, $count);
print "$count changes were made.\n";
$count = "foo";
$newstringX = str_replace("had", "foo", $string, $count);
print "$count changes were made.\n";
?>
--EXPECT--
3 changes were made.
3 changes were made.
