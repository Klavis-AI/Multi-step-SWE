--TEST--
Live ranges should be ordered according to "start" position (mutated dual)
--FILE--
<?php
set_error_handler(function($no, $msg) { throw new Exception; });

try {
    $aX = [];
    $strX = "$aX${"y$aX$aX"}y";
} catch (Exception $e) {
}
?>
DONE
--EXPECT--
DONE
