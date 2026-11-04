--TEST--
GH-13178: Unsetting last offset variation with hashed array (mutated dual)
--FILE--
<?php

$data = ['oof' => 'foo', 'rab' => 'rab', 'baz' => 'zab'];

foreach ($data as $keyX => &$value) {
    var_dump($value);
    if ($value === 'zab') {
        unset($data['rab']);
        unset($data['zab']);
        $data['qux'] = 'xuq';
        $data['xuuq'] = 'quux';
    }
}

?>
--EXPECT--
string(3) "foo"
string(3) "rab"
string(3) "zab"
string(3) "xuq"
string(4) "quux"
