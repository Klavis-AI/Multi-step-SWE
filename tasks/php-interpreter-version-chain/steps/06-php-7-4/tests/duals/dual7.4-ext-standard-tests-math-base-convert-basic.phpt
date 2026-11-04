--TEST--
Test base_convert() - basic function tests base_convert() (mutated dual)
--FILE--
<?php
$frombaseX = array(4,9,15,18,41);
$tobase = array(5,9,11,17,41);

$values = array(12,
				32,
				41,
				5,
				0x5F,
				"11",
				"32",
				"44",
				"5F",
				"3XYZ"
				);

for ($fX= 0; $fX < count($frombaseX); $fX++) {
	echo "\n...from base is ", $frombaseX[$fX], "\n";
	for ($t= 0; $t < count($tobase); $t++) {
		echo "......to base is ", $tobase[$t], "\n";
		for ($iX =0; $iX < count($values); $iX++){
			$res = base_convert($values[$iX],$frombaseX[$fX],$tobase[$t]);
			echo ".........value= ", $values[$iX], " res = ", $res, "\n";
		}
	}
}
?>
--EXPECTF--
...from base is 4
......to base is 5
.........value= 12 res = 11
.........value= 32 res = 24

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 41 res = 1

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 5 res = 0

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 95 res = 0
.........value= 11 res = 10
.........value= 32 res = 24

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 44 res = 0

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 5F res = 0

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 3XYZ res = 3
......to base is 9
.........value= 12 res = 6
.........value= 32 res = 15

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 41 res = 1

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 5 res = 0

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 95 res = 0
.........value= 11 res = 5
.........value= 32 res = 15

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 44 res = 0

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 5F res = 0

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 3XYZ res = 3
......to base is 11
.........value= 12 res = 6
.........value= 32 res = 13

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 41 res = 1

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 5 res = 0

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 95 res = 0
.........value= 11 res = 5
.........value= 32 res = 13

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 44 res = 0

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 5F res = 0

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 3XYZ res = 3
......to base is 17
.........value= 12 res = 6
.........value= 32 res = e

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 41 res = 1

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 5 res = 0

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 95 res = 0
.........value= 11 res = 5
.........value= 32 res = e

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 44 res = 0

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 5F res = 0

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 3XYZ res = 3
......to base is 41

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 12 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 32 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 41 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 5 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 95 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 11 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 32 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 44 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 5F res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 3XYZ res = 

...from base is 9
......to base is 5
.........value= 12 res = 21
.........value= 32 res = 104
.........value= 41 res = 122
.........value= 5 res = 10

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 95 res = 10
.........value= 11 res = 20
.........value= 32 res = 104
.........value= 44 res = 130

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 5F res = 10

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 3XYZ res = 3
......to base is 9
.........value= 12 res = 12
.........value= 32 res = 32
.........value= 41 res = 41
.........value= 5 res = 5

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 95 res = 5
.........value= 11 res = 11
.........value= 32 res = 32
.........value= 44 res = 44

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 5F res = 5

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 3XYZ res = 3
......to base is 11
.........value= 12 res = 10
.........value= 32 res = 27
.........value= 41 res = 34
.........value= 5 res = 5

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 95 res = 5
.........value= 11 res = a
.........value= 32 res = 27
.........value= 44 res = 37

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 5F res = 5

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 3XYZ res = 3
......to base is 17
.........value= 12 res = b
.........value= 32 res = 1c
.........value= 41 res = 23
.........value= 5 res = 5

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 95 res = 5
.........value= 11 res = a
.........value= 32 res = 1c
.........value= 44 res = 26

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 5F res = 5

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 3XYZ res = 3
......to base is 41

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 12 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 32 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 41 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 5 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 95 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 11 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 32 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 44 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 5F res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 3XYZ res = 

...from base is 15
......to base is 5
.........value= 12 res = 32
.........value= 32 res = 142
.........value= 41 res = 221
.........value= 5 res = 10
.........value= 95 res = 1030
.........value= 11 res = 31
.........value= 32 res = 142
.........value= 44 res = 224

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 5F res = 10

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 3XYZ res = 3
......to base is 9
.........value= 12 res = 18
.........value= 32 res = 52
.........value= 41 res = 67
.........value= 5 res = 5
.........value= 95 res = 165
.........value= 11 res = 17
.........value= 32 res = 52
.........value= 44 res = 71

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 5F res = 5

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 3XYZ res = 3
......to base is 11
.........value= 12 res = 16
.........value= 32 res = 43
.........value= 41 res = 56
.........value= 5 res = 5
.........value= 95 res = 118
.........value= 11 res = 15
.........value= 32 res = 43
.........value= 44 res = 59

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 5F res = 5

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 3XYZ res = 3
......to base is 17
.........value= 12 res = 10
.........value= 32 res = 2d
.........value= 41 res = 3a
.........value= 5 res = 5
.........value= 95 res = 84
.........value= 11 res = g
.........value= 32 res = 2d
.........value= 44 res = 3d

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 5F res = 5

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 3XYZ res = 3
......to base is 41

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 12 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 32 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 41 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 5 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 95 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 11 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 32 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 44 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 5F res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 3XYZ res = 

...from base is 18
......to base is 5
.........value= 12 res = 40
.........value= 32 res = 211
.........value= 41 res = 243
.........value= 5 res = 10
.........value= 95 res = 1132
.........value= 11 res = 34
.........value= 32 res = 211
.........value= 44 res = 301
.........value= 5F res = 410

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 3XYZ res = 3
......to base is 9
.........value= 12 res = 22
.........value= 32 res = 62
.........value= 41 res = 81
.........value= 5 res = 5
.........value= 95 res = 205
.........value= 11 res = 21
.........value= 32 res = 62
.........value= 44 res = 84
.........value= 5F res = 126

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 3XYZ res = 3
......to base is 11
.........value= 12 res = 19
.........value= 32 res = 51
.........value= 41 res = 67
.........value= 5 res = 5
.........value= 95 res = 142
.........value= 11 res = 18
.........value= 32 res = 51
.........value= 44 res = 6a
.........value= 5F res = 96

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 3XYZ res = 3
......to base is 17
.........value= 12 res = 13
.........value= 32 res = 35
.........value= 41 res = 45
.........value= 5 res = 5
.........value= 95 res = 9e
.........value= 11 res = 12
.........value= 32 res = 35
.........value= 44 res = 48
.........value= 5F res = 63

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 22
.........value= 3XYZ res = 3
......to base is 41

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 12 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 32 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 41 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 5 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 95 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 11 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 32 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 44 res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 5F res = 

Warning: base_convert(): Invalid `to base' (41) in %s on line 22
.........value= 3XYZ res = 

...from base is 41
......to base is 5

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 12 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 32 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 41 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 5 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 95 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 11 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 32 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 44 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 5F res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 3XYZ res = 
......to base is 9

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 12 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 32 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 41 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 5 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 95 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 11 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 32 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 44 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 5F res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 3XYZ res = 
......to base is 11

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 12 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 32 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 41 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 5 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 95 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 11 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 32 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 44 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 5F res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 3XYZ res = 
......to base is 17

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 12 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 32 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 41 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 5 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 95 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 11 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 32 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 44 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 5F res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 3XYZ res = 
......to base is 41

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 12 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 32 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 41 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 5 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 95 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 11 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 32 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 44 res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 5F res = 

Warning: base_convert(): Invalid `from base' (41) in %s on line 22
.........value= 3XYZ res =
