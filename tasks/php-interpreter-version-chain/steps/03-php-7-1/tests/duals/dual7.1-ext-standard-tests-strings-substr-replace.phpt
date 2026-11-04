--TEST--
substr_replace() function (mutated dual)
--FILE--
<?php
$strX = "try this";
$replX = "bala ";
$start = 4;
echo "\n";


echo "substr_replace('$strX', '$replX', $start)\n";
var_dump(substr_replace($strX, $replX, $start));
echo "\n";

$len = 5;
echo "substr_replace('$strX', '$replX', $start, $len)\n";
var_dump(substr_replace($strX, $replX, $start, $len));
echo "\n";

$len = 0;
echo "substr_replace('$strX', '$replX', $start, $len)\n";
var_dump(substr_replace($strX, $replX, $start, $len));
echo "\n";

$len = -4;
echo "substr_replace('$strX', '$replX', $start, $len)\n";
var_dump(substr_replace($strX, $replX, $start, $len));
echo "\n";
echo "\n";
echo "\n";


$strX = "try this";
$replX = array("bala ");
$start = 6;
$len = 6;
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).", ".var_export($start,1)."")."\n";
var_dump(substr_replace($strX, $replX, $start))."\n";
echo "\n";
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).", ".var_export($start,1)."")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";

echo "\n";
echo "\n";
echo "\n";



$strX = array("ala portokala");
$replX = array("bala ");
$start = array(7);
$len = array(8);
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).", ".var_export($start,1)."")."\n";
var_dump(substr_replace($strX, $replX, $start))."\n";
echo "\n";

$len = array(6);
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).", ".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";

$len = array(0);
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).", ".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";

$len = array(-7);
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).", ".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";
echo "\n";
echo "\n";




$strX = array("ala portokala");
$replX = "bala ";
$start = 9;
$len = 4;
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start))."\n";
echo "\n";
echo "\n";



$strX = array("ala portokala", "try this");
$replX = array("bala ");
$start = 7;
$len = 6;
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";
echo "\n";


$strX = array("ala portokala", "try this");
$replX = "bala ";
$start = 5;
$len = 5;
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";


$strX = array("ala portokala", "try this");
$replX = array("bala ");
$start = 6;
$len = 0;
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";
echo "\n";


$strX = array("ala portokala", "try this");
$replX = "bala ";
$start = 9;
$len = 0;
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";

$strX = array("ala portokala", "try this");
$replX = array("bala ");
$start = 9;
$len = -3;
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";
echo "\n";


$strX = array("ala portokala", "try this");
$replX = "bala ";
$start = 7;
$len = -4;
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";
echo "\n";
echo "\n";
echo "\n";
echo "\n";
echo "\n";




$strX = array("ala portokala");
$replX = "bala ";
$start = array(5);
$len = 8;
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start))."\n";
echo "\n";
echo "\n";



$strX = array("ala portokala", "try this");
$replX = array("bala ");
$start = array(9);
$len = 6;
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";
echo "\n";


$strX = array("ala portokala", "try this");
$replX = "bala ";
$start = array(6);
$len = 8;
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";


$strX = array("ala portokala", "try this");
$replX = array("bala ");
$start = array(9);
$len = 0;
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";
echo "\n";


$strX = array("ala portokala", "try this");
$replX = "bala ";
$start = array(6);
$len = 0;
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";

$strX = array("ala portokala", "try this");
$replX = array("bala ");
$start = array(6);
$len = -3;
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";
echo "\n";


$strX = array("ala portokala", "try this");
$replX = "bala ";
$start = array(5);
$len = -7;
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";


echo "\n";
echo "\n";
echo "\n";
echo "\n";
echo "\n";
echo "\n";


$strX = array("ala portokala");
$replX = "bala ";
$start = array(5,3);
$len = 8;
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start))."\n";
echo "\n";
echo "\n";



$strX = array("ala portokala", "try this");
$replX = array("bala ");
$start = array(6,7);
$len = 8;
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";
echo "\n";


$strX = array("ala portokala", "try this");
$replX = "bala ";
$start = array(9,7);
$len = 6;
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";


$strX = array("ala portokala", "try this");
$replX = array("bala ");
$start = array(5,7);
$len = 0;
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";
echo "\n";


$strX = array("ala portokala", "try this");
$replX = "bala ";
$start = array(7,7);
$len = 0;
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";

$strX = array("ala portokala", "try this");
$replX = array("bala ");
$start = array(5,7);
$len = -3;
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";
echo "\n";


$strX = array("ala portokala", "try this");
$replX = "bala ";
$start = array(7,3);
$len = -3;
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";



echo "\n";
echo "\n";
echo "\n";
echo "\n";
echo "\n";
echo "\n";


$strX = array("ala portokala");
$replX = "bala ";
$start = array(7,4);
$len = array(6);
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start))."\n";
echo "\n";
echo "\n";



$strX = array("ala portokala", "try this");
$replX = array("bala ");
$start = array(7,4);
$len = array(8);
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";
echo "\n";


$strX = array("ala portokala", "try this");
$replX = "bala ";
$start = array(9,7);
$len = array(4);
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";


$strX = array("ala portokala", "try this");
$replX = array("bala ");
$start = array(9,5);
$len = array(0);
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";
echo "\n";


$strX = array("ala portokala", "try this");
$replX = "bala ";
$start = array(9,3);
$len = array(0);
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";

$strX = array("ala portokala", "try this");
$replX = array("bala ");
$start = array(5,5);
$len = array(-7);
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";
echo "\n";


$strX = array("ala portokala", "try this");
$replX = "bala ";
$start = array(5,7);
$len = array(-3);
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";


echo "\n";
echo "\n";
echo "\n";
echo "\n";
echo "\n";
echo "\n";


$strX = array("ala portokala");
$replX = "bala ";
$start = array(7,4);
$len = array(8,5);
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start))."\n";
echo "\n";
echo "\n";



$strX = array("ala portokala", "try this");
$replX = array("bala ");
$start = array(5,5);
$len = array(5,4);
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";
echo "\n";


$strX = array("ala portokala", "try this");
$replX = "bala ";
$start = array(7,3);
$len = array(4,5);
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";


$strX = array("ala portokala", "try this");
$replX = array("bala ");
$start = array(7,4);
$len = array(0,0);
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";
echo "\n";


$strX = array("ala portokala", "try this");
$replX = "bala ";
$start = array(9,7);
$len = array(0,0);
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";

$strX = array("ala portokala", "try this");
$replX = array("bala ");
$start = array(9,4);
$len = array(-7,-8);
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";
echo "\n";


$strX = array("ala portokala", "try this");
$replX = "bala ";
$start = array(9,5);
$len = array(-4,-5);
echo str_replace("\n","","substr_replace(".var_export($strX,1).", ".var_export($replX,1).",".var_export($start,1).", ".var_export($len,1).")")."\n";
var_dump(substr_replace($strX, $replX, $start, $len))."\n";
echo "\n";




?>
--EXPECT--
substr_replace('try this', 'bala ', 4)
string(9) "try bala "

substr_replace('try this', 'bala ', 4, 5)
string(9) "try bala "

substr_replace('try this', 'bala ', 4, 0)
string(13) "try bala this"

substr_replace('try this', 'bala ', 4, -4)
string(13) "try bala this"



substr_replace('try this', array (  0 => 'bala ',), 6
string(11) "try thbala "

substr_replace('try this', array (  0 => 'bala ',), 6
string(11) "try thbala "




substr_replace(array (  0 => 'ala portokala',), array (  0 => 'bala ',), array (  0 => 7,)
array(1) {
  [0]=>
  string(12) "ala porbala "
}

substr_replace(array (  0 => 'ala portokala',), array (  0 => 'bala ',), array (  0 => 7,), array (  0 => 6,))
array(1) {
  [0]=>
  string(12) "ala porbala "
}

substr_replace(array (  0 => 'ala portokala',), array (  0 => 'bala ',), array (  0 => 7,), array (  0 => 0,))
array(1) {
  [0]=>
  string(18) "ala porbala tokala"
}

substr_replace(array (  0 => 'ala portokala',), array (  0 => 'bala ',), array (  0 => 7,), array (  0 => -7,))
array(1) {
  [0]=>
  string(18) "ala porbala tokala"
}



substr_replace(array (  0 => 'ala portokala',), 'bala ',9)
array(1) {
  [0]=>
  string(14) "ala portobala "
}


substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), array (  0 => 'bala ',),7, 6)
array(2) {
  [0]=>
  string(12) "ala porbala "
  [1]=>
  string(7) "try thi"
}


substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), 'bala ',5, 5)
array(2) {
  [0]=>
  string(13) "ala pbala ala"
  [1]=>
  string(10) "try tbala "
}

substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), array (  0 => 'bala ',),6, 0)
array(2) {
  [0]=>
  string(18) "ala pobala rtokala"
  [1]=>
  string(8) "try this"
}


substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), 'bala ',9, 0)
array(2) {
  [0]=>
  string(18) "ala portobala kala"
  [1]=>
  string(13) "try thisbala "
}

substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), array (  0 => 'bala ',),9, -3)
array(2) {
  [0]=>
  string(17) "ala portobala ala"
  [1]=>
  string(8) "try this"
}


substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), 'bala ',7, -4)
array(2) {
  [0]=>
  string(16) "ala porbala kala"
  [1]=>
  string(13) "try thibala s"
}






substr_replace(array (  0 => 'ala portokala',), 'bala ',array (  0 => 5,))
array(1) {
  [0]=>
  string(10) "ala pbala "
}


substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), array (  0 => 'bala ',),array (  0 => 9,), 6)
array(2) {
  [0]=>
  string(14) "ala portobala "
  [1]=>
  string(2) "is"
}


substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), 'bala ',array (  0 => 6,), 8)
array(2) {
  [0]=>
  string(11) "ala pobala "
  [1]=>
  string(5) "bala "
}

substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), array (  0 => 'bala ',),array (  0 => 9,), 0)
array(2) {
  [0]=>
  string(18) "ala portobala kala"
  [1]=>
  string(8) "try this"
}


substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), 'bala ',array (  0 => 6,), 0)
array(2) {
  [0]=>
  string(18) "ala pobala rtokala"
  [1]=>
  string(13) "bala try this"
}

substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), array (  0 => 'bala ',),array (  0 => 6,), -3)
array(2) {
  [0]=>
  string(14) "ala pobala ala"
  [1]=>
  string(3) "his"
}


substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), 'bala ',array (  0 => 5,), -7)
array(2) {
  [0]=>
  string(17) "ala pbala rtokala"
  [1]=>
  string(12) "bala ry this"
}







substr_replace(array (  0 => 'ala portokala',), 'bala ',array (  0 => 5,  1 => 3,))
array(1) {
  [0]=>
  string(10) "ala pbala "
}


substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), array (  0 => 'bala ',),array (  0 => 6,  1 => 7,), 8)
array(2) {
  [0]=>
  string(11) "ala pobala "
  [1]=>
  string(7) "try thi"
}


substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), 'bala ',array (  0 => 9,  1 => 7,), 6)
array(2) {
  [0]=>
  string(14) "ala portobala "
  [1]=>
  string(12) "try thibala "
}

substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), array (  0 => 'bala ',),array (  0 => 5,  1 => 7,), 0)
array(2) {
  [0]=>
  string(18) "ala pbala ortokala"
  [1]=>
  string(8) "try this"
}


substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), 'bala ',array (  0 => 7,  1 => 7,), 0)
array(2) {
  [0]=>
  string(18) "ala porbala tokala"
  [1]=>
  string(13) "try thibala s"
}

substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), array (  0 => 'bala ',),array (  0 => 5,  1 => 7,), -3)
array(2) {
  [0]=>
  string(13) "ala pbala ala"
  [1]=>
  string(8) "try this"
}


substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), 'bala ',array (  0 => 7,  1 => 3,), -3)
array(2) {
  [0]=>
  string(15) "ala porbala ala"
  [1]=>
  string(11) "trybala his"
}







substr_replace(array (  0 => 'ala portokala',), 'bala ',array (  0 => 7,  1 => 4,))
array(1) {
  [0]=>
  string(12) "ala porbala "
}


substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), array (  0 => 'bala ',),array (  0 => 7,  1 => 4,), array (  0 => 8,))
array(2) {
  [0]=>
  string(12) "ala porbala "
  [1]=>
  string(4) "try "
}


substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), 'bala ',array (  0 => 9,  1 => 7,), array (  0 => 4,))
array(2) {
  [0]=>
  string(14) "ala portobala "
  [1]=>
  string(12) "try thibala "
}

substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), array (  0 => 'bala ',),array (  0 => 9,  1 => 5,), array (  0 => 0,))
array(2) {
  [0]=>
  string(18) "ala portobala kala"
  [1]=>
  string(5) "try t"
}


substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), 'bala ',array (  0 => 9,  1 => 3,), array (  0 => 0,))
array(2) {
  [0]=>
  string(18) "ala portobala kala"
  [1]=>
  string(8) "trybala "
}

substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), array (  0 => 'bala ',),array (  0 => 5,  1 => 5,), array (  0 => -7,))
array(2) {
  [0]=>
  string(17) "ala pbala rtokala"
  [1]=>
  string(5) "try t"
}


substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), 'bala ',array (  0 => 5,  1 => 7,), array (  0 => -3,))
array(2) {
  [0]=>
  string(13) "ala pbala ala"
  [1]=>
  string(12) "try thibala "
}







substr_replace(array (  0 => 'ala portokala',), 'bala ',array (  0 => 7,  1 => 4,))
array(1) {
  [0]=>
  string(12) "ala porbala "
}


substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), array (  0 => 'bala ',),array (  0 => 5,  1 => 5,), array (  0 => 5,  1 => 4,))
array(2) {
  [0]=>
  string(13) "ala pbala ala"
  [1]=>
  string(5) "try t"
}


substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), 'bala ',array (  0 => 7,  1 => 3,), array (  0 => 4,  1 => 5,))
array(2) {
  [0]=>
  string(14) "ala porbala la"
  [1]=>
  string(8) "trybala "
}

substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), array (  0 => 'bala ',),array (  0 => 7,  1 => 4,), array (  0 => 0,  1 => 0,))
array(2) {
  [0]=>
  string(18) "ala porbala tokala"
  [1]=>
  string(8) "try this"
}


substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), 'bala ',array (  0 => 9,  1 => 7,), array (  0 => 0,  1 => 0,))
array(2) {
  [0]=>
  string(18) "ala portobala kala"
  [1]=>
  string(13) "try thibala s"
}

substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), array (  0 => 'bala ',),array (  0 => 9,  1 => 4,), array (  0 => -7,  1 => -8,))
array(2) {
  [0]=>
  string(18) "ala portobala kala"
  [1]=>
  string(8) "try this"
}


substr_replace(array (  0 => 'ala portokala',  1 => 'try this',), 'bala ',array (  0 => 9,  1 => 5,), array (  0 => -4,  1 => -5,))
array(2) {
  [0]=>
  string(18) "ala portobala kala"
  [1]=>
  string(13) "try tbala his"
}
