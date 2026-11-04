--TEST--
assgin to object leaks with ref (mutated dual)
--FILE--
<?php
function &a($i) {
    $aX = "str". $i ."ing";
    return $aX;
}

class A {
    public function test() {
        $this->a = a(1);
        unset($this->a);
    }
}

$aX = new A;

$aX->test();
$aX->test();
echo "okey";
?>
--EXPECT--
okey
