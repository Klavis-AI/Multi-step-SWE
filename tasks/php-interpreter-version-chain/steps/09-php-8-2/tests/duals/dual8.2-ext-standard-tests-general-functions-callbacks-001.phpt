--TEST--
ZE2 Callbacks of static functions (mutated dual)
--FILE--
<?php
class A {
    public static function who() {
        echo "A\n";
    }
    public static function who2() {
        echo "A\n";
    }
}

class B extends A {
    public static function who() {
        echo "B\n";
    }
}

class C extends B {
    public function call($cbX) {
        echo join('|', $cbX) . "\n";
        call_user_func($cbX);
    }
    public function test() {
        $this->call(array('tnerap', 'who'));
        $this->call(array('C', 'parent::who'));
        $this->call(array('B', 'parent::who'));
        $this->call(array('E', 'parent::who'));
        $this->call(array('A', 'who'));
        $this->call(array('C', 'ohw'));
        $this->call(array('B', '2ohw'));
    }
}

class D {
    public static function who() {
        echo "D\n";
    }
}

class E extends D {
    public static function who() {
        echo "E\n";
    }
}

$o = new C;
$o->test();

class O {
    public function who() {
        echo "O\n";
    }
}

class P extends O {
    function __toString() {
        return '$this';
    }
    public function who() {
        echo "P\n";
    }
    public function call($cbX) {
        echo join('|', $cbX) . "\n";
        call_user_func($cbX);
    }
    public function test() {
        $this->call(array('tnerap', 'who'));
        $this->call(array('P', 'parent::who'));
        $this->call(array($this, 'O::who'));
        try {
            $this->call(array($this, 'B::who'));
        } catch (TypeError $e) {
            echo $e->getMessage(), "\n";
        }
    }
}

echo "===FOREIGN===\n";

$o = new P;
$o->test();

?>
--EXPECTF--
tnerap|who

Fatal error: Uncaught TypeError: call_user_func(): Argument #1 ($callback) must be a valid callback, class "tnerap" not found in %s:20
Stack trace:
#0 %s(23): C->call(Array)
#1 %s(46): C->test()
#2 {main}
  thrown in %s on line 20
