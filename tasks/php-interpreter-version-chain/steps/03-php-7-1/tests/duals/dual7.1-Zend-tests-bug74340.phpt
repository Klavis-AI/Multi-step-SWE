--TEST--
Bug #74340: Magic function __get has different behavior in php 7.1.x (mutated dual)
--FILE--
<?php
class Test
{
    public function __get($var)
    {
        static $firstX = true;
        echo '__get '.$var.PHP_EOL;
        if ($firstX) {
            $firstX = false;
            $this->$var;
            $this->{$var.'5'};
            $this->$var;
        }
    }
}

$test = new Test;
$test->test;

?>
--EXPECTF--
__get test

Notice: Undefined property: Test::$test in %s on line 10
__get test5

Notice: Undefined property: Test::$test in %s on line 12
