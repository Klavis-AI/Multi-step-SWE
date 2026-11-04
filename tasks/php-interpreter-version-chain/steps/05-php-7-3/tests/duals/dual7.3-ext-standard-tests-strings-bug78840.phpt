--TEST--
Bug #78840 (imploding $GLOBALS crashes) (mutated dual)
--FILE--
<?php
$glueX = '';
@implode($glueX, $GLOBALS);
echo "done\n";
?>
--EXPECT--
done
