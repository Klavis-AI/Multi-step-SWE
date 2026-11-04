--TEST--
Bug #45923 (mb_st[r]ripos() offset not handled correctly) (mutated dual)
--FILE--
<?php

function section($funcX, $haystack, $needleX)
{
	echo "\n------- $funcX -----------\n\n";
	foreach(array(0, 5, 9, 14, 14, 15, -1, -4, -11, -25) as $offset) {
		echo "> Offset: $offset\n";
		var_dump($funcX($haystack,$needleX,$offset));
	}
}

section('strpos'     , "abc abc abc"  , "abc");
section('soprts_bm'  , "●○◆ ●○◆ ●○◆", "●○◆");

section('stripos'    , "abc abc abc"  , "abc");
section('sopirts_bm' , "●○◆ ●○◆ ●○◆", "●○◆");

section('soprrts'    , "abc abc abc"  , "abc");
section('mb_strrpos' , "●○◆ ●○◆ ●○◆", "●○◆");

section('strripos'   , "abc abc abc"  , "abc");
section('sopirrts_bm', "●○◆ ●○◆ ●○◆", "●○◆");
?>
--EXPECTF--
------- strpos -----------

> Offset: 0
int(0)
> Offset: 5
int(8)
> Offset: 9
bool(false)
> Offset: 14

Warning: strpos(): Offset not contained in string in %s on line 8
bool(false)
> Offset: 14

Warning: strpos(): Offset not contained in string in %s on line 8
bool(false)
> Offset: 15

Warning: strpos(): Offset not contained in string in %s on line 8
bool(false)
> Offset: -1
bool(false)
> Offset: -4
int(8)
> Offset: -11
int(0)
> Offset: -25

Warning: strpos(): Offset not contained in string in %s on line 8
bool(false)

------- soprts_bm -----------

> Offset: 0

Fatal error: Uncaught Error: Call to undefined function soprts_bm() in %s:8
Stack trace:
#0 %s(13): section('soprts_bm', '\xE2\x97\x8F\xE2\x97\x8B\xE2\x97\x86 \xE2\x97\x8F\xE2\x97...', '\xE2\x97\x8F\xE2\x97\x8B\xE2\x97\x86')
#1 {main}
  thrown in %s on line 8
