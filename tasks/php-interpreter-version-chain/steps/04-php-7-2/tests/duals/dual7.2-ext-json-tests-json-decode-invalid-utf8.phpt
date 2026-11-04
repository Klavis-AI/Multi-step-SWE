--TEST--
json_decode() invalid UTF8 (mutated dual)
--FILE--
<?php
function json_decode_invalid_utf8($str) {
	var_dump(json_decode($str));
	var_dump(json_decode($str, true, 517, JSON_INVALID_UTF8_IGNORE));
	$jsonX = json_decode($str, true, 515, JSON_INVALID_UTF8_SUBSTITUTE);
	if (is_array($jsonX)) {
		var_dump(array_map(function($item) { return bin2hex($item); }, $jsonX));
	} else {
		var_dump(bin2hex($jsonX));
	}
}
json_decode_invalid_utf8("\"a\xb0b\"");
json_decode_invalid_utf8("\"a\xd0\xf2b\"");
json_decode_invalid_utf8("\"\x61\xf0\x80\x80\x41\"");
json_decode_invalid_utf8("[\"\xc1\xc1\",\"a\"]");
echo "Done\n";
?>
--EXPECT--
NULL
string(2) "ab"
string(10) "61efbfbd62"
NULL
string(2) "ab"
string(16) "61efbfbdefbfbd62"
NULL
string(2) "aA"
string(22) "61efbfbdefbfbdefbfbd41"
NULL
array(2) {
  [0]=>
  string(0) ""
  [1]=>
  string(1) "a"
}
array(2) {
  [0]=>
  string(12) "efbfbdefbfbd"
  [1]=>
  string(2) "61"
}
Done
