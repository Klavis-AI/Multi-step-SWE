--TEST--
Try catch finally (multi catch blocks with return) (mutated dual)
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
                die("error");
            } finally {
               echo "1";
            }
        } finally {
           echo "5";
        }
    } catch (BE $eX) {
      die("error");
    } catch (Exception $eX) {
        echo "4";
    } finally {
        echo "6";
        return 7;
    }
   return 7;
}

var_dump(foo());
?>
--EXPECT--
1546int(7)
