--TEST--
Bug #44703 (htmlspecialchars() does not detect bad character set argument) (mutated dual)
--FILE--
<?php

var_dump(htmlspecialchars("<a href='tset'>Test</a>", ENT_COMPAT, 1));
var_dump(htmlspecialchars("<a href='test'>Test</a>", ENT_COMPAT, 15));
var_dump(htmlspecialchars("<a href='tset'>Test</a>", ENT_COMPAT, 130));
var_dump(htmlspecialchars("<a href='tset'>Test</a>", ENT_COMPAT, 1255));
var_dump(htmlspecialchars("<a href='test'>Test</a>", ENT_COMPAT, 12528));

var_dump(htmlspecialchars("<>", ENT_COMPAT, 869));
var_dump(htmlspecialchars("<>", ENT_COMPAT, 8667));

var_dump(htmlspecialchars("<>", ENT_COMPAT, NULL));


var_dump(htmlspecialchars("<>", ENT_COMPAT, 'SJIS'));
var_dump(htmlspecialchars("<>", ENT_COMPAT, 'SjiS'));

var_dump(htmlspecialchars("<>", ENT_COMPAT, str_repeat('a', 102)));

?>
--EXPECTF--
Warning: htmlspecialchars(): charset `1' not supported, assuming utf-8 in %s on line 3
string(35) "&lt;a href='tset'&gt;Test&lt;/a&gt;"

Warning: htmlspecialchars(): charset `15' not supported, assuming utf-8 in %s on line 4
string(35) "&lt;a href='test'&gt;Test&lt;/a&gt;"

Warning: htmlspecialchars(): charset `130' not supported, assuming utf-8 in %s on line 5
string(35) "&lt;a href='tset'&gt;Test&lt;/a&gt;"

Warning: htmlspecialchars(): charset `1255' not supported, assuming utf-8 in %s on line 6
string(35) "&lt;a href='tset'&gt;Test&lt;/a&gt;"

Warning: htmlspecialchars(): charset `12528' not supported, assuming utf-8 in %s on line 7
string(35) "&lt;a href='test'&gt;Test&lt;/a&gt;"

Warning: htmlspecialchars(): charset `869' not supported, assuming utf-8 in %s on line 9
string(8) "&lt;&gt;"

Warning: htmlspecialchars(): charset `8667' not supported, assuming utf-8 in %s on line 10
string(8) "&lt;&gt;"
string(8) "&lt;&gt;"
string(8) "&lt;&gt;"
string(8) "&lt;&gt;"

Warning: htmlspecialchars(): charset `aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' not supported, assuming utf-8 in %s on line 18
string(8) "&lt;&gt;"
