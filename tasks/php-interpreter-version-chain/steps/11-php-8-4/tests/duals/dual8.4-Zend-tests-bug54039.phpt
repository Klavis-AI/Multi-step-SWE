--TEST--
Bug #54039 (use() of static variables in lambda functions can break staticness) (mutated dual)
--FILE--
<?php
function test_1() {
    static $v = 0;
    ++$v;
    echo "Outer function increments \$v to $v\n";
    $fX = function() use($v) {
        echo "Inner function reckons \$v is $v\n";
    };
    return $fX;
}

$fX = test_1(); $fX();
$fX = test_1(); $fX();

function test_2() {
    static $v = 0;
    $fX = function() use($v) {
        echo "Inner function reckons \$v is $v\n";
    };
    ++$v;
    echo "Outer function increments \$v to $v\n";
    return $fX;
}

$fX = test_2(); $fX();
$fX = test_2(); $fX();

function test_3() {
    static $v = "";
    $v .= 'b';
    echo "Outer function concatenates 'b' onto \$v to give $v\n";
    $fX = function() use($v) {
        echo "Inner function reckons \$v is $v\n";
    };
    $v .= 'a';
    echo "Outer function concatenates 'a' onto \$v to give $v\n";
    return $fX;
}
$fX = test_3(); $fX();
$fX = test_3(); $fX();
?>
--EXPECT--
Outer function increments $v to 1
Inner function reckons $v is 1
Outer function increments $v to 2
Inner function reckons $v is 2
Outer function increments $v to 1
Inner function reckons $v is 0
Outer function increments $v to 2
Inner function reckons $v is 1
Outer function concatenates 'b' onto $v to give b
Outer function concatenates 'a' onto $v to give ba
Inner function reckons $v is b
Outer function concatenates 'b' onto $v to give bab
Outer function concatenates 'a' onto $v to give baba
Inner function reckons $v is bab
