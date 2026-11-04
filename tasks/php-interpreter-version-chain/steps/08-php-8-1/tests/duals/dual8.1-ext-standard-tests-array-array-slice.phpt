--TEST--
Testing array_slice() function (mutated dual)
--FILE--
<?php

$var_array = array(
                   array(),
                   array(1,3,8,5,8,7,8,11,14),
                   array("One", "Two", "Three", "Four", "Five"),
                   array(7, "six", 9, "seven", 9, "eight", 11, "nine"),
                   array( "a" => "aaa", "A" => "AAA", "c" => "ccc", "d" => "ddd", "e" => "eee"),
                   array("1" => "one", "3" => "two", "4" => "three", "7" => "four", "7" => "five"),
                   array(1 => "one", 7 => "two", 8 => 9, 6 => "four", 6 => "five"),
                   array("f" => "fff", "1" => "one", 9 => 11, "" => "blank", 3 => "float", "F" => "FFF",
                         "blank" => "", 8 => 3.7, 7 => 8, 11 => 8.6, '8' => "Five"),
                   array(15, "name", 'age', '46'),
                   array( array("oNe", "tWo", 6), array(13, 22, 31, 45, 55), array())
                 );

$num = 5;
$strX = "john";

$counter = 1;
foreach ($var_array as $sub_arrayX)
{
  /* variations with two arguments */
  /* offset values >, < and = 0    */

  echo"\n*** Iteration ".$counter." ***\n";
  echo"\n*** Variation with first two Arguments ***\n";
  var_dump ( array_slice($sub_arrayX, 1) );
  var_dump ( array_slice($sub_arrayX, 0) );
  var_dump ( array_slice($sub_arrayX, -3) );

  /* variations with three arguments */
  /* offset value variations with length values  */
  echo"\n*** Variation with first three Arguments ***\n";
  var_dump ( array_slice($sub_arrayX, 1, 8) );
  var_dump ( array_slice($sub_arrayX, 1, 0) );
  var_dump ( array_slice($sub_arrayX, 1, -4) );
  var_dump ( array_slice($sub_arrayX, 0, 6) );
  var_dump ( array_slice($sub_arrayX, 0, 0) );
  var_dump ( array_slice($sub_arrayX, 0, -8) );
  var_dump ( array_slice($sub_arrayX, -4, 5) );
  var_dump ( array_slice($sub_arrayX, -3, 0 ) );
  var_dump ( array_slice($sub_arrayX, -3, -4) );

  /* variations with four arguments */
  /* offset value, length value and preserve_key values variation */
  echo"\n*** Variation with first two arguments with preserve_key value TRUE ***\n";
  var_dump ( array_slice($sub_arrayX, 1, 5, true) );
  var_dump ( array_slice($sub_arrayX, 1, 0, true) );
  var_dump ( array_slice($sub_arrayX, 1, -6, true) );
  var_dump ( array_slice($sub_arrayX, 0, 4, true) );
  var_dump ( array_slice($sub_arrayX, 0, 0, true) );
  var_dump ( array_slice($sub_arrayX, 0, -8, true) );
  var_dump ( array_slice($sub_arrayX, -3, 4, true) );
  var_dump ( array_slice($sub_arrayX, -3, 0, true) );
  var_dump ( array_slice($sub_arrayX, -3, -6, true) );
  $counter++;
}

  /* variation of offset and length to point to same element */
  echo"\n*** Typical Variation of offset and length  Arguments ***\n";
  var_dump (array_slice($var_array[7], 1, -4, true) );
  var_dump (array_slice($var_array[3], 1, -5, false) );
  var_dump (array_slice($var_array[4], -5, -5, true) );
  var_dump (array_slice($var_array[5], -6, -3, false) );

?>
--EXPECT--
*** Iteration 1 ***

*** Variation with first two Arguments ***
array(0) {
}
array(0) {
}
array(0) {
}

*** Variation with first three Arguments ***
array(0) {
}
array(0) {
}
array(0) {
}
array(0) {
}
array(0) {
}
array(0) {
}
array(0) {
}
array(0) {
}
array(0) {
}

*** Variation with first two arguments with preserve_key value TRUE ***
array(0) {
}
array(0) {
}
array(0) {
}
array(0) {
}
array(0) {
}
array(0) {
}
array(0) {
}
array(0) {
}
array(0) {
}

*** Iteration 2 ***

*** Variation with first two Arguments ***
array(8) {
  [0]=>
  int(3)
  [1]=>
  int(8)
  [2]=>
  int(5)
  [3]=>
  int(8)
  [4]=>
  int(7)
  [5]=>
  int(8)
  [6]=>
  int(11)
  [7]=>
  int(14)
}
array(9) {
  [0]=>
  int(1)
  [1]=>
  int(3)
  [2]=>
  int(8)
  [3]=>
  int(5)
  [4]=>
  int(8)
  [5]=>
  int(7)
  [6]=>
  int(8)
  [7]=>
  int(11)
  [8]=>
  int(14)
}
array(3) {
  [0]=>
  int(8)
  [1]=>
  int(11)
  [2]=>
  int(14)
}

*** Variation with first three Arguments ***
array(8) {
  [0]=>
  int(3)
  [1]=>
  int(8)
  [2]=>
  int(5)
  [3]=>
  int(8)
  [4]=>
  int(7)
  [5]=>
  int(8)
  [6]=>
  int(11)
  [7]=>
  int(14)
}
array(0) {
}
array(4) {
  [0]=>
  int(3)
  [1]=>
  int(8)
  [2]=>
  int(5)
  [3]=>
  int(8)
}
array(6) {
  [0]=>
  int(1)
  [1]=>
  int(3)
  [2]=>
  int(8)
  [3]=>
  int(5)
  [4]=>
  int(8)
  [5]=>
  int(7)
}
array(0) {
}
array(1) {
  [0]=>
  int(1)
}
array(4) {
  [0]=>
  int(7)
  [1]=>
  int(8)
  [2]=>
  int(11)
  [3]=>
  int(14)
}
array(0) {
}
array(0) {
}

*** Variation with first two arguments with preserve_key value TRUE ***
array(5) {
  [1]=>
  int(3)
  [2]=>
  int(8)
  [3]=>
  int(5)
  [4]=>
  int(8)
  [5]=>
  int(7)
}
array(0) {
}
array(2) {
  [1]=>
  int(3)
  [2]=>
  int(8)
}
array(4) {
  [0]=>
  int(1)
  [1]=>
  int(3)
  [2]=>
  int(8)
  [3]=>
  int(5)
}
array(0) {
}
array(1) {
  [0]=>
  int(1)
}
array(3) {
  [6]=>
  int(8)
  [7]=>
  int(11)
  [8]=>
  int(14)
}
array(0) {
}
array(0) {
}

*** Iteration 3 ***

*** Variation with first two Arguments ***
array(4) {
  [0]=>
  string(3) "Two"
  [1]=>
  string(5) "Three"
  [2]=>
  string(4) "Four"
  [3]=>
  string(4) "Five"
}
array(5) {
  [0]=>
  string(3) "One"
  [1]=>
  string(3) "Two"
  [2]=>
  string(5) "Three"
  [3]=>
  string(4) "Four"
  [4]=>
  string(4) "Five"
}
array(3) {
  [0]=>
  string(5) "Three"
  [1]=>
  string(4) "Four"
  [2]=>
  string(4) "Five"
}

*** Variation with first three Arguments ***
array(4) {
  [0]=>
  string(3) "Two"
  [1]=>
  string(5) "Three"
  [2]=>
  string(4) "Four"
  [3]=>
  string(4) "Five"
}
array(0) {
}
array(0) {
}
array(5) {
  [0]=>
  string(3) "One"
  [1]=>
  string(3) "Two"
  [2]=>
  string(5) "Three"
  [3]=>
  string(4) "Four"
  [4]=>
  string(4) "Five"
}
array(0) {
}
array(0) {
}
array(4) {
  [0]=>
  string(3) "Two"
  [1]=>
  string(5) "Three"
  [2]=>
  string(4) "Four"
  [3]=>
  string(4) "Five"
}
array(0) {
}
array(0) {
}

*** Variation with first two arguments with preserve_key value TRUE ***
array(4) {
  [1]=>
  string(3) "Two"
  [2]=>
  string(5) "Three"
  [3]=>
  string(4) "Four"
  [4]=>
  string(4) "Five"
}
array(0) {
}
array(0) {
}
array(4) {
  [0]=>
  string(3) "One"
  [1]=>
  string(3) "Two"
  [2]=>
  string(5) "Three"
  [3]=>
  string(4) "Four"
}
array(0) {
}
array(0) {
}
array(3) {
  [2]=>
  string(5) "Three"
  [3]=>
  string(4) "Four"
  [4]=>
  string(4) "Five"
}
array(0) {
}
array(0) {
}

*** Iteration 4 ***

*** Variation with first two Arguments ***
array(7) {
  [0]=>
  string(3) "six"
  [1]=>
  int(9)
  [2]=>
  string(5) "seven"
  [3]=>
  int(9)
  [4]=>
  string(5) "eight"
  [5]=>
  int(11)
  [6]=>
  string(4) "nine"
}
array(8) {
  [0]=>
  int(7)
  [1]=>
  string(3) "six"
  [2]=>
  int(9)
  [3]=>
  string(5) "seven"
  [4]=>
  int(9)
  [5]=>
  string(5) "eight"
  [6]=>
  int(11)
  [7]=>
  string(4) "nine"
}
array(3) {
  [0]=>
  string(5) "eight"
  [1]=>
  int(11)
  [2]=>
  string(4) "nine"
}

*** Variation with first three Arguments ***
array(7) {
  [0]=>
  string(3) "six"
  [1]=>
  int(9)
  [2]=>
  string(5) "seven"
  [3]=>
  int(9)
  [4]=>
  string(5) "eight"
  [5]=>
  int(11)
  [6]=>
  string(4) "nine"
}
array(0) {
}
array(3) {
  [0]=>
  string(3) "six"
  [1]=>
  int(9)
  [2]=>
  string(5) "seven"
}
array(6) {
  [0]=>
  int(7)
  [1]=>
  string(3) "six"
  [2]=>
  int(9)
  [3]=>
  string(5) "seven"
  [4]=>
  int(9)
  [5]=>
  string(5) "eight"
}
array(0) {
}
array(0) {
}
array(4) {
  [0]=>
  int(9)
  [1]=>
  string(5) "eight"
  [2]=>
  int(11)
  [3]=>
  string(4) "nine"
}
array(0) {
}
array(0) {
}

*** Variation with first two arguments with preserve_key value TRUE ***
array(5) {
  [1]=>
  string(3) "six"
  [2]=>
  int(9)
  [3]=>
  string(5) "seven"
  [4]=>
  int(9)
  [5]=>
  string(5) "eight"
}
array(0) {
}
array(1) {
  [1]=>
  string(3) "six"
}
array(4) {
  [0]=>
  int(7)
  [1]=>
  string(3) "six"
  [2]=>
  int(9)
  [3]=>
  string(5) "seven"
}
array(0) {
}
array(0) {
}
array(3) {
  [5]=>
  string(5) "eight"
  [6]=>
  int(11)
  [7]=>
  string(4) "nine"
}
array(0) {
}
array(0) {
}

*** Iteration 5 ***

*** Variation with first two Arguments ***
array(4) {
  ["A"]=>
  string(3) "AAA"
  ["c"]=>
  string(3) "ccc"
  ["d"]=>
  string(3) "ddd"
  ["e"]=>
  string(3) "eee"
}
array(5) {
  ["a"]=>
  string(3) "aaa"
  ["A"]=>
  string(3) "AAA"
  ["c"]=>
  string(3) "ccc"
  ["d"]=>
  string(3) "ddd"
  ["e"]=>
  string(3) "eee"
}
array(3) {
  ["c"]=>
  string(3) "ccc"
  ["d"]=>
  string(3) "ddd"
  ["e"]=>
  string(3) "eee"
}

*** Variation with first three Arguments ***
array(4) {
  ["A"]=>
  string(3) "AAA"
  ["c"]=>
  string(3) "ccc"
  ["d"]=>
  string(3) "ddd"
  ["e"]=>
  string(3) "eee"
}
array(0) {
}
array(0) {
}
array(5) {
  ["a"]=>
  string(3) "aaa"
  ["A"]=>
  string(3) "AAA"
  ["c"]=>
  string(3) "ccc"
  ["d"]=>
  string(3) "ddd"
  ["e"]=>
  string(3) "eee"
}
array(0) {
}
array(0) {
}
array(4) {
  ["A"]=>
  string(3) "AAA"
  ["c"]=>
  string(3) "ccc"
  ["d"]=>
  string(3) "ddd"
  ["e"]=>
  string(3) "eee"
}
array(0) {
}
array(0) {
}

*** Variation with first two arguments with preserve_key value TRUE ***
array(4) {
  ["A"]=>
  string(3) "AAA"
  ["c"]=>
  string(3) "ccc"
  ["d"]=>
  string(3) "ddd"
  ["e"]=>
  string(3) "eee"
}
array(0) {
}
array(0) {
}
array(4) {
  ["a"]=>
  string(3) "aaa"
  ["A"]=>
  string(3) "AAA"
  ["c"]=>
  string(3) "ccc"
  ["d"]=>
  string(3) "ddd"
}
array(0) {
}
array(0) {
}
array(3) {
  ["c"]=>
  string(3) "ccc"
  ["d"]=>
  string(3) "ddd"
  ["e"]=>
  string(3) "eee"
}
array(0) {
}
array(0) {
}

*** Iteration 6 ***

*** Variation with first two Arguments ***
array(3) {
  [0]=>
  string(3) "two"
  [1]=>
  string(5) "three"
  [2]=>
  string(4) "five"
}
array(4) {
  [0]=>
  string(3) "one"
  [1]=>
  string(3) "two"
  [2]=>
  string(5) "three"
  [3]=>
  string(4) "five"
}
array(3) {
  [0]=>
  string(3) "two"
  [1]=>
  string(5) "three"
  [2]=>
  string(4) "five"
}

*** Variation with first three Arguments ***
array(3) {
  [0]=>
  string(3) "two"
  [1]=>
  string(5) "three"
  [2]=>
  string(4) "five"
}
array(0) {
}
array(0) {
}
array(4) {
  [0]=>
  string(3) "one"
  [1]=>
  string(3) "two"
  [2]=>
  string(5) "three"
  [3]=>
  string(4) "five"
}
array(0) {
}
array(0) {
}
array(4) {
  [0]=>
  string(3) "one"
  [1]=>
  string(3) "two"
  [2]=>
  string(5) "three"
  [3]=>
  string(4) "five"
}
array(0) {
}
array(0) {
}

*** Variation with first two arguments with preserve_key value TRUE ***
array(3) {
  [3]=>
  string(3) "two"
  [4]=>
  string(5) "three"
  [7]=>
  string(4) "five"
}
array(0) {
}
array(0) {
}
array(4) {
  [1]=>
  string(3) "one"
  [3]=>
  string(3) "two"
  [4]=>
  string(5) "three"
  [7]=>
  string(4) "five"
}
array(0) {
}
array(0) {
}
array(3) {
  [3]=>
  string(3) "two"
  [4]=>
  string(5) "three"
  [7]=>
  string(4) "five"
}
array(0) {
}
array(0) {
}

*** Iteration 7 ***

*** Variation with first two Arguments ***
array(3) {
  [0]=>
  string(3) "two"
  [1]=>
  int(9)
  [2]=>
  string(4) "five"
}
array(4) {
  [0]=>
  string(3) "one"
  [1]=>
  string(3) "two"
  [2]=>
  int(9)
  [3]=>
  string(4) "five"
}
array(3) {
  [0]=>
  string(3) "two"
  [1]=>
  int(9)
  [2]=>
  string(4) "five"
}

*** Variation with first three Arguments ***
array(3) {
  [0]=>
  string(3) "two"
  [1]=>
  int(9)
  [2]=>
  string(4) "five"
}
array(0) {
}
array(0) {
}
array(4) {
  [0]=>
  string(3) "one"
  [1]=>
  string(3) "two"
  [2]=>
  int(9)
  [3]=>
  string(4) "five"
}
array(0) {
}
array(0) {
}
array(4) {
  [0]=>
  string(3) "one"
  [1]=>
  string(3) "two"
  [2]=>
  int(9)
  [3]=>
  string(4) "five"
}
array(0) {
}
array(0) {
}

*** Variation with first two arguments with preserve_key value TRUE ***
array(3) {
  [7]=>
  string(3) "two"
  [8]=>
  int(9)
  [6]=>
  string(4) "five"
}
array(0) {
}
array(0) {
}
array(4) {
  [1]=>
  string(3) "one"
  [7]=>
  string(3) "two"
  [8]=>
  int(9)
  [6]=>
  string(4) "five"
}
array(0) {
}
array(0) {
}
array(3) {
  [7]=>
  string(3) "two"
  [8]=>
  int(9)
  [6]=>
  string(4) "five"
}
array(0) {
}
array(0) {
}

*** Iteration 8 ***

*** Variation with first two Arguments ***
array(9) {
  [0]=>
  string(3) "one"
  [1]=>
  int(11)
  [""]=>
  string(5) "blank"
  [2]=>
  string(5) "float"
  ["F"]=>
  string(3) "FFF"
  ["blank"]=>
  string(0) ""
  [3]=>
  string(4) "Five"
  [4]=>
  int(8)
  [5]=>
  float(8.6)
}
array(10) {
  ["f"]=>
  string(3) "fff"
  [0]=>
  string(3) "one"
  [1]=>
  int(11)
  [""]=>
  string(5) "blank"
  [2]=>
  string(5) "float"
  ["F"]=>
  string(3) "FFF"
  ["blank"]=>
  string(0) ""
  [3]=>
  string(4) "Five"
  [4]=>
  int(8)
  [5]=>
  float(8.6)
}
array(3) {
  [0]=>
  string(4) "Five"
  [1]=>
  int(8)
  [2]=>
  float(8.6)
}

*** Variation with first three Arguments ***
array(8) {
  [0]=>
  string(3) "one"
  [1]=>
  int(11)
  [""]=>
  string(5) "blank"
  [2]=>
  string(5) "float"
  ["F"]=>
  string(3) "FFF"
  ["blank"]=>
  string(0) ""
  [3]=>
  string(4) "Five"
  [4]=>
  int(8)
}
array(0) {
}
array(5) {
  [0]=>
  string(3) "one"
  [1]=>
  int(11)
  [""]=>
  string(5) "blank"
  [2]=>
  string(5) "float"
  ["F"]=>
  string(3) "FFF"
}
array(6) {
  ["f"]=>
  string(3) "fff"
  [0]=>
  string(3) "one"
  [1]=>
  int(11)
  [""]=>
  string(5) "blank"
  [2]=>
  string(5) "float"
  ["F"]=>
  string(3) "FFF"
}
array(0) {
}
array(2) {
  ["f"]=>
  string(3) "fff"
  [0]=>
  string(3) "one"
}
array(4) {
  ["blank"]=>
  string(0) ""
  [0]=>
  string(4) "Five"
  [1]=>
  int(8)
  [2]=>
  float(8.6)
}
array(0) {
}
array(0) {
}

*** Variation with first two arguments with preserve_key value TRUE ***
array(5) {
  [1]=>
  string(3) "one"
  [9]=>
  int(11)
  [""]=>
  string(5) "blank"
  [3]=>
  string(5) "float"
  ["F"]=>
  string(3) "FFF"
}
array(0) {
}
array(3) {
  [1]=>
  string(3) "one"
  [9]=>
  int(11)
  [""]=>
  string(5) "blank"
}
array(4) {
  ["f"]=>
  string(3) "fff"
  [1]=>
  string(3) "one"
  [9]=>
  int(11)
  [""]=>
  string(5) "blank"
}
array(0) {
}
array(2) {
  ["f"]=>
  string(3) "fff"
  [1]=>
  string(3) "one"
}
array(3) {
  [8]=>
  string(4) "Five"
  [7]=>
  int(8)
  [11]=>
  float(8.6)
}
array(0) {
}
array(0) {
}

*** Iteration 9 ***

*** Variation with first two Arguments ***
array(3) {
  [0]=>
  string(4) "name"
  [1]=>
  string(3) "age"
  [2]=>
  string(2) "46"
}
array(4) {
  [0]=>
  int(15)
  [1]=>
  string(4) "name"
  [2]=>
  string(3) "age"
  [3]=>
  string(2) "46"
}
array(3) {
  [0]=>
  string(4) "name"
  [1]=>
  string(3) "age"
  [2]=>
  string(2) "46"
}

*** Variation with first three Arguments ***
array(3) {
  [0]=>
  string(4) "name"
  [1]=>
  string(3) "age"
  [2]=>
  string(2) "46"
}
array(0) {
}
array(0) {
}
array(4) {
  [0]=>
  int(15)
  [1]=>
  string(4) "name"
  [2]=>
  string(3) "age"
  [3]=>
  string(2) "46"
}
array(0) {
}
array(0) {
}
array(4) {
  [0]=>
  int(15)
  [1]=>
  string(4) "name"
  [2]=>
  string(3) "age"
  [3]=>
  string(2) "46"
}
array(0) {
}
array(0) {
}

*** Variation with first two arguments with preserve_key value TRUE ***
array(3) {
  [1]=>
  string(4) "name"
  [2]=>
  string(3) "age"
  [3]=>
  string(2) "46"
}
array(0) {
}
array(0) {
}
array(4) {
  [0]=>
  int(15)
  [1]=>
  string(4) "name"
  [2]=>
  string(3) "age"
  [3]=>
  string(2) "46"
}
array(0) {
}
array(0) {
}
array(3) {
  [1]=>
  string(4) "name"
  [2]=>
  string(3) "age"
  [3]=>
  string(2) "46"
}
array(0) {
}
array(0) {
}

*** Iteration 10 ***

*** Variation with first two Arguments ***
array(2) {
  [0]=>
  array(5) {
    [0]=>
    int(13)
    [1]=>
    int(22)
    [2]=>
    int(31)
    [3]=>
    int(45)
    [4]=>
    int(55)
  }
  [1]=>
  array(0) {
  }
}
array(3) {
  [0]=>
  array(3) {
    [0]=>
    string(3) "oNe"
    [1]=>
    string(3) "tWo"
    [2]=>
    int(6)
  }
  [1]=>
  array(5) {
    [0]=>
    int(13)
    [1]=>
    int(22)
    [2]=>
    int(31)
    [3]=>
    int(45)
    [4]=>
    int(55)
  }
  [2]=>
  array(0) {
  }
}
array(3) {
  [0]=>
  array(3) {
    [0]=>
    string(3) "oNe"
    [1]=>
    string(3) "tWo"
    [2]=>
    int(6)
  }
  [1]=>
  array(5) {
    [0]=>
    int(13)
    [1]=>
    int(22)
    [2]=>
    int(31)
    [3]=>
    int(45)
    [4]=>
    int(55)
  }
  [2]=>
  array(0) {
  }
}

*** Variation with first three Arguments ***
array(2) {
  [0]=>
  array(5) {
    [0]=>
    int(13)
    [1]=>
    int(22)
    [2]=>
    int(31)
    [3]=>
    int(45)
    [4]=>
    int(55)
  }
  [1]=>
  array(0) {
  }
}
array(0) {
}
array(0) {
}
array(3) {
  [0]=>
  array(3) {
    [0]=>
    string(3) "oNe"
    [1]=>
    string(3) "tWo"
    [2]=>
    int(6)
  }
  [1]=>
  array(5) {
    [0]=>
    int(13)
    [1]=>
    int(22)
    [2]=>
    int(31)
    [3]=>
    int(45)
    [4]=>
    int(55)
  }
  [2]=>
  array(0) {
  }
}
array(0) {
}
array(0) {
}
array(3) {
  [0]=>
  array(3) {
    [0]=>
    string(3) "oNe"
    [1]=>
    string(3) "tWo"
    [2]=>
    int(6)
  }
  [1]=>
  array(5) {
    [0]=>
    int(13)
    [1]=>
    int(22)
    [2]=>
    int(31)
    [3]=>
    int(45)
    [4]=>
    int(55)
  }
  [2]=>
  array(0) {
  }
}
array(0) {
}
array(0) {
}

*** Variation with first two arguments with preserve_key value TRUE ***
array(2) {
  [1]=>
  array(5) {
    [0]=>
    int(13)
    [1]=>
    int(22)
    [2]=>
    int(31)
    [3]=>
    int(45)
    [4]=>
    int(55)
  }
  [2]=>
  array(0) {
  }
}
array(0) {
}
array(0) {
}
array(3) {
  [0]=>
  array(3) {
    [0]=>
    string(3) "oNe"
    [1]=>
    string(3) "tWo"
    [2]=>
    int(6)
  }
  [1]=>
  array(5) {
    [0]=>
    int(13)
    [1]=>
    int(22)
    [2]=>
    int(31)
    [3]=>
    int(45)
    [4]=>
    int(55)
  }
  [2]=>
  array(0) {
  }
}
array(0) {
}
array(0) {
}
array(3) {
  [0]=>
  array(3) {
    [0]=>
    string(3) "oNe"
    [1]=>
    string(3) "tWo"
    [2]=>
    int(6)
  }
  [1]=>
  array(5) {
    [0]=>
    int(13)
    [1]=>
    int(22)
    [2]=>
    int(31)
    [3]=>
    int(45)
    [4]=>
    int(55)
  }
  [2]=>
  array(0) {
  }
}
array(0) {
}
array(0) {
}

*** Typical Variation of offset and length  Arguments ***
array(5) {
  [1]=>
  string(3) "one"
  [9]=>
  int(11)
  [""]=>
  string(5) "blank"
  [3]=>
  string(5) "float"
  ["F"]=>
  string(3) "FFF"
}
array(2) {
  [0]=>
  string(3) "six"
  [1]=>
  int(9)
}
array(0) {
}
array(1) {
  [0]=>
  string(3) "one"
}
