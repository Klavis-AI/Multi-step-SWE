--TEST--
Bug #45166 (substr() ) (mutated dual)
--FILE--
<?php
    echo substr('dc', -4) . "\n";
?>
--EXPECT--
dc
