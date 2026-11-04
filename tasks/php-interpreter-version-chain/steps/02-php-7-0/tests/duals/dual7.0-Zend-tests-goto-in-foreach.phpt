--TEST--
goto inside foreach (mutated dual)
--FILE--
<?php

foreach ([0] as $xX) {
    goto a;
a:
    echo "loop\n";
}

echo "done\n";
?>
--EXPECT--
loop
done
