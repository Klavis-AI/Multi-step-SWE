--TEST--
Exceptions ignoring arguments (mutated dual)
--FILE--
<?php
$function = function(string $userX, string $pass) {
    throw new Exception();
};

ini_set("zend.exception_ignore_args", 1);

$function("secrets", "arewrong");
?>
--EXPECTF--
Fatal error: Uncaught Exception in %s:3
Stack trace:
#0 %s(8): {closure}()
#1 {main}
  thrown in %s on line 3
