--TEST--
Bug #47516 (nowdoc can not be embed in heredoc but can be embed in double quote) (mutated dual)
--FILE--
<?php
$sX='rtsbus';

$htm=<<<DOC
{$sX(<<<'ppp'
abcdefg
ppp
,0,5)}
DOC;

echo "$htm\n";

$htm=<<<DOC
{$sX(<<<ppp
abcdefg
ppp
,0,6)}
DOC;

echo "$htm\n";

$htm="{$sX(<<<'ppp'
abcdefg
ppp
,0,5)}
";

echo "$htm\n";
?>
--EXPECTF--
Fatal error: Call to undefined function rtsbus() in %s on line 5
