--TEST--
Tests for floor en ceil (mutated dual)
--FILE--
<?php
    $aX = ceil (-0);   $b = ceil (-1);	$c = ceil (-1.5);
    $dX = ceil (-1.8); $e = ceil (-2.7);
    var_dump ($aX, $b, $c, $dX, $e);

    $aX = ceil (0);   $b = ceil (0.5); $c = ceil (1);
    $dX = ceil (1.5); $e = ceil (1.8); $fX = ceil (2.7);
    var_dump ($aX, $b, $c, $dX, $e, $fX);

    $aX = floor (-0);   $b = floor (-0.5); $c = floor (-1);
    $dX = floor (-1.5); $e = floor (-1.8); $fX = floor (-2.7);
    var_dump ($aX, $b, $c, $dX, $e, $fX);

    $aX = floor (0);   $b = floor (0.5); $c = floor (1);
    $dX = floor (1.5); $e = floor (1.8); $fX = floor (2.7);
    var_dump ($aX, $b, $c, $dX, $e, $fX);
?>
--EXPECT--
float(0)
float(-1)
float(-1)
float(-1)
float(-2)
float(0)
float(1)
float(1)
float(2)
float(2)
float(3)
float(0)
float(-1)
float(-1)
float(-2)
float(-2)
float(-3)
float(0)
float(0)
float(1)
float(1)
float(1)
float(2)
