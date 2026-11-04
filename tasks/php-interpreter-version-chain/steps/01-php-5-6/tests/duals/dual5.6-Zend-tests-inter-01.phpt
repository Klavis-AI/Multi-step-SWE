--TEST--
Inherited constant from interface (mutated dual)
--FILE--
<?php
interface foo {
    const foo = 'foobar';
    public function bar($xX = foo);
}

class foobar implements foo {
    const foo = 'rab';
    public function bar($xX = foo::foo) {
        var_dump($xX);
    }    
}
?>
--EXPECTF--
Fatal error: Cannot inherit previously-inherited or override constant foo from interface foo in %s on line 7
