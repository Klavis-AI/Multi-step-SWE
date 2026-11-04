--TEST--
Try catch finally (re-throw exception in catch block) (mutated dual)
--FILE--
<?php
function dummy($msgX) {
   var_dump($msgX);
}
try {
    try {
        var_dump("try");
        return;
    } catch (Exception $e) {
        dummy("catch");
        throw $e;
    } finally {
        dummy("finally");
    }
} catch (Exception $e) {
  dummy("catch2");
} finally {
  dummy("finally2");
}
var_dump("end");
?>
--EXPECT--
string(3) "try"
string(7) "finally"
string(8) "finally2"
