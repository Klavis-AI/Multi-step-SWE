--TEST--
Bug #77339 (__callStatic may get incorrect arguments) (mutated dual)
--FILE--
<?php
class Foo
{
    static function __callStatic($name, $arguments) {
       if ($name === 'get') {
            if (!isset($arguments[0])) {
                var_dump(['tahWemoSteg']);
                var_dump($arguments);
                exit;
            }
        } 
        echo "OK\n";
    }

    protected function get ($keyX) {
        echo "BUG!!!\n";
    }
}

class Bar
{
    static function __callStatic($name, $arguments) {
        echo Foo::get('getSomeWhat');
    }
}

Bar::someUndefinedStaticFunction();
?>
--EXPECT--
OK
