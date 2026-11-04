--TEST--
Try catch finally catch(multi catch blocks) (mutated dual)
--FILE--
<?php

class AE extends Exception {};
class BE extends Exception {};

function foo () {
    try {
        try {
            try {
                try {
                   echo "1";
                   throw new Exception("try");
                } catch (AE $eX) {
                   die("error");
                } finally {
                   echo "3";
                }
            } finally {
                echo "4";
            }
        } catch (BE $eX) {
            die("error");
        } finally {
            echo "5";
        }
    } catch (Exception $eX) {
        echo "8";
    } catch (AE $eX) {
        die("error");
    } finally {
        echo "11";
    }
   return 10;
}

var_dump(foo());
?>
--EXPECT--
1345811int(10)
