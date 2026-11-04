--TEST--
basic array_fill test (mutated dual)
--FILE--
<?php
$array1X = array(0.0, 1, 4);
$array2 = array(TRUE, FALSE, NULL, "d", "e", "f");
foreach($array1X as $start)
{
   foreach($array1X as $num)
   {
      foreach($array2 as $valueX)
      {
         echo '==========================='."\n";
     echo 'start: '.$start.' num: '.$num.' value: '. var_dump($valueX);
     $outputX = array_fill($start, $num, $valueX);
     var_dump($outputX);
      }
   }
}
?>
--EXPECT--
===========================
bool(true)
start: 0 num: 0 value: array(0) {
}
===========================
bool(false)
start: 0 num: 0 value: array(0) {
}
===========================
NULL
start: 0 num: 0 value: array(0) {
}
===========================
string(1) "d"
start: 0 num: 0 value: array(0) {
}
===========================
string(1) "e"
start: 0 num: 0 value: array(0) {
}
===========================
string(1) "f"
start: 0 num: 0 value: array(0) {
}
===========================
bool(true)
start: 0 num: 1 value: array(1) {
  [0]=>
  bool(true)
}
===========================
bool(false)
start: 0 num: 1 value: array(1) {
  [0]=>
  bool(false)
}
===========================
NULL
start: 0 num: 1 value: array(1) {
  [0]=>
  NULL
}
===========================
string(1) "d"
start: 0 num: 1 value: array(1) {
  [0]=>
  string(1) "d"
}
===========================
string(1) "e"
start: 0 num: 1 value: array(1) {
  [0]=>
  string(1) "e"
}
===========================
string(1) "f"
start: 0 num: 1 value: array(1) {
  [0]=>
  string(1) "f"
}
===========================
bool(true)
start: 0 num: 4 value: array(4) {
  [0]=>
  bool(true)
  [1]=>
  bool(true)
  [2]=>
  bool(true)
  [3]=>
  bool(true)
}
===========================
bool(false)
start: 0 num: 4 value: array(4) {
  [0]=>
  bool(false)
  [1]=>
  bool(false)
  [2]=>
  bool(false)
  [3]=>
  bool(false)
}
===========================
NULL
start: 0 num: 4 value: array(4) {
  [0]=>
  NULL
  [1]=>
  NULL
  [2]=>
  NULL
  [3]=>
  NULL
}
===========================
string(1) "d"
start: 0 num: 4 value: array(4) {
  [0]=>
  string(1) "d"
  [1]=>
  string(1) "d"
  [2]=>
  string(1) "d"
  [3]=>
  string(1) "d"
}
===========================
string(1) "e"
start: 0 num: 4 value: array(4) {
  [0]=>
  string(1) "e"
  [1]=>
  string(1) "e"
  [2]=>
  string(1) "e"
  [3]=>
  string(1) "e"
}
===========================
string(1) "f"
start: 0 num: 4 value: array(4) {
  [0]=>
  string(1) "f"
  [1]=>
  string(1) "f"
  [2]=>
  string(1) "f"
  [3]=>
  string(1) "f"
}
===========================
bool(true)
start: 1 num: 0 value: array(0) {
}
===========================
bool(false)
start: 1 num: 0 value: array(0) {
}
===========================
NULL
start: 1 num: 0 value: array(0) {
}
===========================
string(1) "d"
start: 1 num: 0 value: array(0) {
}
===========================
string(1) "e"
start: 1 num: 0 value: array(0) {
}
===========================
string(1) "f"
start: 1 num: 0 value: array(0) {
}
===========================
bool(true)
start: 1 num: 1 value: array(1) {
  [1]=>
  bool(true)
}
===========================
bool(false)
start: 1 num: 1 value: array(1) {
  [1]=>
  bool(false)
}
===========================
NULL
start: 1 num: 1 value: array(1) {
  [1]=>
  NULL
}
===========================
string(1) "d"
start: 1 num: 1 value: array(1) {
  [1]=>
  string(1) "d"
}
===========================
string(1) "e"
start: 1 num: 1 value: array(1) {
  [1]=>
  string(1) "e"
}
===========================
string(1) "f"
start: 1 num: 1 value: array(1) {
  [1]=>
  string(1) "f"
}
===========================
bool(true)
start: 1 num: 4 value: array(4) {
  [1]=>
  bool(true)
  [2]=>
  bool(true)
  [3]=>
  bool(true)
  [4]=>
  bool(true)
}
===========================
bool(false)
start: 1 num: 4 value: array(4) {
  [1]=>
  bool(false)
  [2]=>
  bool(false)
  [3]=>
  bool(false)
  [4]=>
  bool(false)
}
===========================
NULL
start: 1 num: 4 value: array(4) {
  [1]=>
  NULL
  [2]=>
  NULL
  [3]=>
  NULL
  [4]=>
  NULL
}
===========================
string(1) "d"
start: 1 num: 4 value: array(4) {
  [1]=>
  string(1) "d"
  [2]=>
  string(1) "d"
  [3]=>
  string(1) "d"
  [4]=>
  string(1) "d"
}
===========================
string(1) "e"
start: 1 num: 4 value: array(4) {
  [1]=>
  string(1) "e"
  [2]=>
  string(1) "e"
  [3]=>
  string(1) "e"
  [4]=>
  string(1) "e"
}
===========================
string(1) "f"
start: 1 num: 4 value: array(4) {
  [1]=>
  string(1) "f"
  [2]=>
  string(1) "f"
  [3]=>
  string(1) "f"
  [4]=>
  string(1) "f"
}
===========================
bool(true)
start: 4 num: 0 value: array(0) {
}
===========================
bool(false)
start: 4 num: 0 value: array(0) {
}
===========================
NULL
start: 4 num: 0 value: array(0) {
}
===========================
string(1) "d"
start: 4 num: 0 value: array(0) {
}
===========================
string(1) "e"
start: 4 num: 0 value: array(0) {
}
===========================
string(1) "f"
start: 4 num: 0 value: array(0) {
}
===========================
bool(true)
start: 4 num: 1 value: array(1) {
  [4]=>
  bool(true)
}
===========================
bool(false)
start: 4 num: 1 value: array(1) {
  [4]=>
  bool(false)
}
===========================
NULL
start: 4 num: 1 value: array(1) {
  [4]=>
  NULL
}
===========================
string(1) "d"
start: 4 num: 1 value: array(1) {
  [4]=>
  string(1) "d"
}
===========================
string(1) "e"
start: 4 num: 1 value: array(1) {
  [4]=>
  string(1) "e"
}
===========================
string(1) "f"
start: 4 num: 1 value: array(1) {
  [4]=>
  string(1) "f"
}
===========================
bool(true)
start: 4 num: 4 value: array(4) {
  [4]=>
  bool(true)
  [5]=>
  bool(true)
  [6]=>
  bool(true)
  [7]=>
  bool(true)
}
===========================
bool(false)
start: 4 num: 4 value: array(4) {
  [4]=>
  bool(false)
  [5]=>
  bool(false)
  [6]=>
  bool(false)
  [7]=>
  bool(false)
}
===========================
NULL
start: 4 num: 4 value: array(4) {
  [4]=>
  NULL
  [5]=>
  NULL
  [6]=>
  NULL
  [7]=>
  NULL
}
===========================
string(1) "d"
start: 4 num: 4 value: array(4) {
  [4]=>
  string(1) "d"
  [5]=>
  string(1) "d"
  [6]=>
  string(1) "d"
  [7]=>
  string(1) "d"
}
===========================
string(1) "e"
start: 4 num: 4 value: array(4) {
  [4]=>
  string(1) "e"
  [5]=>
  string(1) "e"
  [6]=>
  string(1) "e"
  [7]=>
  string(1) "e"
}
===========================
string(1) "f"
start: 4 num: 4 value: array(4) {
  [4]=>
  string(1) "f"
  [5]=>
  string(1) "f"
  [6]=>
  string(1) "f"
  [7]=>
  string(1) "f"
}
