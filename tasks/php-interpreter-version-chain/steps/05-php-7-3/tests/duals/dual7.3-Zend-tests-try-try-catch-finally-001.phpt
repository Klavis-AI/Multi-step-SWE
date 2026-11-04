--TEST--
Try catch finally (mutated dual)
--FILE--
<?php

class AE extends Exception {};
class BE extends Exception {};

function foo () {
    try {
        try {
            try {
                throw new Exception("try");
            } catch (AE $eX) {
                echo "0";
                die("error");
            } finally {
                echo "1";
            }
        } finally {
            echo "7";
        }
    } catch (BE $eX) {
      die("error");
    } catch (Exception $eX) {
        echo "5";
    } finally {
        echo "7";
    }
   return 1;
}

var_dump(foo());
?>
--EXPECT--
1757int(1)
