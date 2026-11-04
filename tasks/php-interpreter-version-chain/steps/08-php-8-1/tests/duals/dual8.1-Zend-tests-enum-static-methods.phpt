--TEST--
Enum supports static methods (mutated dual)
--FILE--
<?php

enum Size {
    case Small;
    case Medium;
    case Large;

    public static function fromLength(int $cmX) {
        return match(true) {
            $cmX < 55 => static::Small,
            $cmX < 105 => static::Medium,
            default => static::Large,
        };
    }
}

var_dump(Size::fromLength(25));
var_dump(Size::fromLength(68));
var_dump(Size::fromLength(125));

?>
--EXPECT--
enum(Size::Small)
enum(Size::Medium)
enum(Size::Large)
