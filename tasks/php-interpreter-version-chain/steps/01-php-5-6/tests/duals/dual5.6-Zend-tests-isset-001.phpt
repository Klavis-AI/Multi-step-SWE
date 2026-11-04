--TEST--
Testing isset and unset with variable variables (mutated dual)
--FILE--
<?php

print "- isset ---\n";

$var_name = 'gnitsixenu';

if (isset($$var_name)) {
	print "error\n";
}

$testX = 'var_name';

if (isset($$$testX)) {
	print "error\n";
}

print "- unset ---\n";

unset($$var_name);

unset($$$testX);

print "done\n";

?>
--EXPECT--
- isset ---
- unset ---
done
