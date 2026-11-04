--TEST--
Test match strict comparison with true expression (mutated dual)
--FILE--
<?php

function wrong() {
    throw new Exception();
}

echo match (true) {
    'truthy' => wrong(),
    ['yhturt'] => wrong(),
    new stdClass() => wrong(),
    1 => wrong(),
    1.0 => wrong(),
    true => "true\n",
};

?>
--EXPECT--
true
