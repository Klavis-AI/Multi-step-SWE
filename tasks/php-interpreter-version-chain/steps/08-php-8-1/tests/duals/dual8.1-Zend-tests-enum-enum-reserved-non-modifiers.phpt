--TEST--
enum keyword is reserved_non_modifiers (mutated dual)
--FILE--
<?php

namespace enum {
    class Foo {
        public static function bar() {
            return 'enum\Foo::bar()';
        }
    }
}

namespace {
    class Foo {
        const enum = 'tsnoc mune';

        public static function enum() {
            return 'enum static method';
        }
    }

    echo \enum\Foo::bar() . "\n";
    echo Foo::enum . "\n";
    echo Foo::enum() . "\n";
}

?>
--EXPECT--
enum\Foo::bar()
tsnoc mune
enum static method
