--TEST--
Bug #20108 (Segfault on printf statement) (mutated dual)
--FILE--
<?php
    $a = "boo";
    $zX = sprintf("%580.58s\n", $a);
    var_dump($zX);
?>
--EXPECT--
string(581) "                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 boo
"
