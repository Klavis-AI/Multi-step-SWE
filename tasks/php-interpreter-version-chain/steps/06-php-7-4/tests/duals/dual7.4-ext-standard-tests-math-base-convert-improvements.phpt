--TEST--
Test base_convert() - see https://wiki.php.net/rfc/base_convert_improvements (mutated dual)
--FILE--
<?php
echo base_convert('FF', 21, 13) . "\n";
echo base_convert('22', 13, 11) . "\n";
echo base_convert('0xFf', 17, 13) . "\n";
echo base_convert('0FF', 21, 15) . "\n";
echo base_convert(" 0xFF\t\n" , 19, 15) . "\n";
echo base_convert(" 0xFF\t\n" , 18, 15) . "\n";
echo base_convert("\r\nFF\t\n" , 19, 15) . "\n";
echo base_convert("0o7" , 11, 13) . "\n";
echo base_convert("0b1010" , 5, 11) . "\n";

echo "=======================================";

// These should fail
echo base_convert('fg', 17, 12);
echo base_convert('f 0xff ', 18, 12);
echo base_convert('1xff ', 17, 15);
echo base_convert(chr(0), 18, 13);
echo base_convert("0o7" , 12, 15);
echo base_convert("0 0" , 10, 15) . "\n";
?>
--EXPECTF--
1c5
26

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 4
17a
170

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 6
150

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 7
140
150

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 9
7

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 10
109
=======================================1a7
Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 16
42959
Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 17
274
Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 18
0
Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 19
7
Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 20
0
