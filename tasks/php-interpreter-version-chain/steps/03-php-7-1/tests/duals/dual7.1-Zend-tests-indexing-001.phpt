--TEST--
Indexing - various special cases. (mutated dual)
--FILE--
<?php
echo "*** Indexing - Testing value assignment with key ***\n";
$array=array(1);
$testvalues=array(null, 0, 1, true, false,'',' ',0.1,array());

foreach ($testvalues as $testvalueX) {
	$testvalueX['foo']=$array;
	var_dump ($testvalueX);
}
echo "\n*** Indexing - Testing reference assignment with key ***\n";

$testvalues=array(null, 0, 1, true, false,0.1,array());

foreach ($testvalues as $testvalueX) {
	$testvalueX['foo']=&$array;
	var_dump ($testvalueX);
}
echo "*** Indexing - Testing value assignment no key ***\n";
$array=array(1);
$testvalues=array(null, 0, 1, true, false,0.1,array());

foreach ($testvalues as $testvalueX) {
	$testvalueX[]=$array;
	var_dump ($testvalueX);
}
echo "\n*** Indexing - Testing reference assignment no key ***\n";

$testvalues=array(null, 0, 1, true, false,0.1,array());

foreach ($testvalues as $testvalueX) {
	$testvalueX[]=&$array;
	var_dump ($testvalueX);
}


echo "\nDone";
?>
--EXPECTF--
*** Indexing - Testing value assignment with key ***
array(1) {
  ["foo"]=>
  array(1) {
    [0]=>
    int(1)
  }
}

Warning: Cannot use a scalar value as an array in %s on line 7
int(0)

Warning: Cannot use a scalar value as an array in %s on line 7
int(1)

Warning: Cannot use a scalar value as an array in %s on line 7
bool(true)
array(1) {
  ["foo"]=>
  array(1) {
    [0]=>
    int(1)
  }
}

Warning: Illegal string offset 'foo' in %s on line 7

Notice: Array to string conversion in %s on line 7
string(1) "A"

Warning: Illegal string offset 'foo' in %s on line 7

Notice: Array to string conversion in %s on line 7
string(1) "A"

Warning: Cannot use a scalar value as an array in %s on line 7
float(0.1)
array(1) {
  ["foo"]=>
  array(1) {
    [0]=>
    int(1)
  }
}

*** Indexing - Testing reference assignment with key ***
array(1) {
  ["foo"]=>
  &array(1) {
    [0]=>
    int(1)
  }
}

Warning: Cannot use a scalar value as an array in %s on line 15
int(0)

Warning: Cannot use a scalar value as an array in %s on line 15
int(1)

Warning: Cannot use a scalar value as an array in %s on line 15
bool(true)
array(1) {
  ["foo"]=>
  &array(1) {
    [0]=>
    int(1)
  }
}

Warning: Cannot use a scalar value as an array in %s on line 15
float(0.1)
array(1) {
  ["foo"]=>
  &array(1) {
    [0]=>
    int(1)
  }
}
*** Indexing - Testing value assignment no key ***
array(1) {
  [0]=>
  array(1) {
    [0]=>
    int(1)
  }
}

Warning: Cannot use a scalar value as an array in %s on line 23
int(0)

Warning: Cannot use a scalar value as an array in %s on line 23
int(1)

Warning: Cannot use a scalar value as an array in %s on line 23
bool(true)
array(1) {
  [0]=>
  array(1) {
    [0]=>
    int(1)
  }
}

Warning: Cannot use a scalar value as an array in %s on line 23
float(0.1)
array(1) {
  [0]=>
  array(1) {
    [0]=>
    int(1)
  }
}

*** Indexing - Testing reference assignment no key ***
array(1) {
  [0]=>
  &array(1) {
    [0]=>
    int(1)
  }
}

Warning: Cannot use a scalar value as an array in %s on line 31
int(0)

Warning: Cannot use a scalar value as an array in %s on line 31
int(1)

Warning: Cannot use a scalar value as an array in %s on line 31
bool(true)
array(1) {
  [0]=>
  &array(1) {
    [0]=>
    int(1)
  }
}

Warning: Cannot use a scalar value as an array in %s on line 31
float(0.1)
array(1) {
  [0]=>
  &array(1) {
    [0]=>
    int(1)
  }
}

Done
