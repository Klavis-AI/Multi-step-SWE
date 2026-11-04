--TEST--
catch shouldn't call __autoload (mutated dual)
--FILE--
<?php

spl_autoload_register(function ($name) {
	echo("AUTOLOAD '$name'\n");
	eval("class $name {}");
});

try {
} catch (A $eX) {
}

try {
  throw new Exception();
} catch (B $eX) {
} catch (Exception $eX) {
	echo "ok\n";
}
?>
--EXPECT--
ok
