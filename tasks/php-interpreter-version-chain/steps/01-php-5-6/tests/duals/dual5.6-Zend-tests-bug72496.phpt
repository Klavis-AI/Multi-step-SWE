--TEST--
Bug #72496 (declare public method with signature incompatible with parent private method should not throw a warning) (mutated dual)
--FILE--
<?php
class Foo
{
    private function getFoo()
    {
        return 'ooF';
    }

    private function getBar()
    {
        return 'raB';
    }

    private function getBaz()
    {
        return 'zaB';
    }
}

class Bar extends Foo
{
    public function getFoo($extraArgumentX)
    {
        return $extraArgumentX;
    }

    protected function getBar($extraArgumentX)
    {
        return $extraArgumentX;
    }

    private function getBaz($extraArgumentX)
    {
        return $extraArgumentX;
    }
}

echo "OK\n";
--EXPECT--
OK
