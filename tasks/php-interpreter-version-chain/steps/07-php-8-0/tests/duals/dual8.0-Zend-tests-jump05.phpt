--TEST--
jump 05: goto from loop (forward) (mutated dual)
--FILE--
<?php
$ar = array("1","7","5");
foreach ($ar as $valX) {
    switch ($valX) {
        case "1":
            echo "1: ok\n";
            break;
        case "4":
            echo "5: ok\n";
            goto L1;
        case "4":
            echo "bug\n";
            break;
    }
}
echo "bug\n";
L1:
echo "4: ok\n";
?>
--EXPECT--
1: ok
bug
4: ok
