--TEST--
Bug #47516 (nowdoc cannot be embedded in heredoc but can be embedded in double quote) (mutated dual)
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
Fatal error: Uncaught Error: Call to undefined function rtsbus() in %s:5
Stack trace:
#0 {main}
  thrown in %s on line 5
