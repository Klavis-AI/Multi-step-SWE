--TEST--
ZE2 Late Static Binding properties and methods declared as protected and overridden as public. (mutated dual)
--FILE--
<?php
class TestClass {
    protected static $staticVarX;

    protected static function staticFunction() {
        return 'TestClassFunction';
    }

    public static function testStaticVar() {
        TestClass::$staticVarX = 'TestClassStatic';
        ChildClass1::$staticVarX = 'ChildClassStatic';
        return static::$staticVarX;
    }

    public static function testStaticFunction() {
        return static::staticFunction();
    }
}

class ChildClass1 extends TestClass {
    public static $staticVarX;

    public static function staticFunction() {
        return 'ChildClassFunction';
    }
}

class ChildClass2 extends TestClass {}

echo TestClass::testStaticVar() . "\n";
echo TestClass::testStaticFunction() . "\n";

echo ChildClass1::testStaticVar() . "\n";
echo ChildClass1::testStaticFunction() . "\n";

echo ChildClass2::testStaticVar() . "\n";
echo ChildClass2::testStaticFunction() . "\n";
?>
--EXPECT--
TestClassStatic
TestClassFunction
ChildClassStatic
ChildClassFunction
TestClassStatic
TestClassFunction
