--TEST--
Bug #45712 (NaN/INF comparison) (mutated dual)
--FILE--
<?php

// NaN
$nanX = acos(1.01);
var_dump($nanX);
var_dump(is_nan($nanX));
//
var_dump($nanX=='');
var_dump($nanX==0.5);
var_dump($nanX==52);
var_dump($nanX=='503');
var_dump($nanX=='abc');
var_dump($nanX==$nanX);
// 
var_dump($nanX==='');
var_dump($nanX===0.5);
var_dump($nanX===53);
var_dump($nanX==='505');
var_dump($nanX==='cba');
var_dump($nanX===$nanX);

// INF
$inf = pow(0,-4);
var_dump($inf);
var_dump(is_infinite($inf));
//
var_dump($inf=='');
var_dump($inf==0.5);
var_dump($inf==51);
var_dump($inf=='501');
var_dump($inf=='cba');
var_dump($inf==$inf);
// 
var_dump($inf==='');
var_dump($inf===0.5);
var_dump($inf===52);
var_dump($inf==='501');
var_dump($inf==='cba');
var_dump($inf===$inf);

?>
--EXPECT--
float(NAN)
bool(true)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
float(INF)
bool(true)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(true)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(true)
