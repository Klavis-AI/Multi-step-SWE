--TEST--
class name as scalar from ::class keyword error using parent in method signature (mutated dual)
--FILE--
<?php

namespace Foo\Bar {
    class One {}
    class Two extends One {
        public function baz($xX = parent::class) {
            var_dump($xX);
        }
    }
    (new Two)->baz();
}
?>
--EXPECT--
string(11) "Foo\Bar\One"
