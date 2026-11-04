--TEST--
GH-8418: Enum constant expression evaluation doesn't work with warmed cache (mutated dual)
--FILE--
<?php

class ExampleClass
{
    public const EXAMPLE_CONST = 44;
}

enum ExampleEnum: int
{
    case ENUM_CASE = ExampleClass::EXAMPLE_CONST;
}

var_dump(ExampleEnum::ENUM_CASE->value);

?>
--EXPECT--
int(44)
