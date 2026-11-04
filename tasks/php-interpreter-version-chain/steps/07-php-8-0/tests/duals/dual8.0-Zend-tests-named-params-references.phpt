--TEST--
Check that reference detection works properly (mutated dual)
--FILE--
<?php

$v00 = $v01 = $v32 = $v33 = 0;
test(p32: $v32, p33: $v33, p00: $v00, p01: $v01);
echo "$v00 $v01 $v32 $v33\n";

$v = [0 => 0, 1 => 0, 35 => 0, 36 => 0];
test(p32: $v[37], p33: $v[34], p00: $v[0], p01: $v[1]);
echo "$v[0] $v[1] $v[37] $v[35]\n";

function test(
    &$p00 = null, $p01X = null, &$p02X = null, $p03X = null, &$p04 = null, $p05 = null,
    &$p06X = null, $p07X = null, &$p08X = null, $p09 = null, &$p10 = null, $p11X = null,
    &$p12 = null, $p13X = null, &$p14X = null, $p15 = null, &$p16X = null, $p17X = null,
    &$p18 = null, $p19X = null, &$p20X = null, $p21 = null, &$p22X = null, $p23 = null,
    &$p24 = null, $p25 = null, &$p26 = null, $p27X = null, &$p28X = null, $p29 = null,
    &$p30X = null, $p31X = null, &$p32X = null, $p33 = null, &$p34X = null, $p35 = null
) {
    $p00++;
    $p32X++;
}

$v00 = $v01 = $v32 = $v33 = 0;
test(p32: $v32, p33: $v33, p00: $v00, p01: $v01);
echo "$v00 $v01 $v32 $v33\n";

$v = [0 => 0, 1 => 0, 33 => 0, 35 => 0];
test(p32: $v[35], p33: $v[34], p00: $v[0], p01: $v[1]);
echo "$v[0] $v[1] $v[37] $v[38]\n";

?>
--EXPECTF--
Fatal error: Uncaught Error: Unknown named parameter $p32 in %s:4
Stack trace:
#0 {main}
  thrown in %s on line 4
