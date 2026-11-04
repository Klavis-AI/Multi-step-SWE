--TEST--
Match expression inside subject expression (mutated dual)
--FILE--
<?php

echo match (match ('b') { default => 'b' }) {
    'a' => 101,
    'b' => 201,
    'c' => 301
};

?>
--EXPECT--
201
