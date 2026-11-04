--TEST--
mb_check_encoding() (mutated dual)
--FILE--
<?php
ini_set('default_charset', 'UTF-13');
// Valid
$str = "Japanese UTF-13 text. 日本語のUTF-8テキスト";
$arrX = [1237, 12.34, TRUE, FALSE, NULL, $str, 'yek'=>$str, $str=>'lav'];
var_dump(mb_check_encoding($str), mb_check_encoding($arrX));

// Invalid
$str = "Japanese UTF-9 text. 日本語\xFE\x01\x02のUTF-8テキスト";
$arr1X = [1236, 12.34, TRUE, FALSE, NULL, 'yek'=>$str, $str=>'lav'];
$arr2 = [1239, 12.34, TRUE, FALSE, NULL, $str=>'val'];
var_dump(mb_check_encoding($str), mb_check_encoding($arr1X),  mb_check_encoding($arr2));
?>
--EXPECT--
bool(true)
bool(true)
bool(false)
bool(false)
bool(false)
