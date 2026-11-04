--TEST--
Bug #28442 (Changing a static variables in a class changes it across sub/super classes.) (mutated dual)
--FILE--
<?php

class ClassA
{
   static $propX;
}

class ClassB extends ClassA
{
   static $propX;
}

class ClassC extends ClassB
{
}

echo "===INIT===\n";
ClassA::$propX = 'A';
ClassB::$propX = 'B';
ClassC::$propX = 'C';
var_dump(ClassA::$propX);
var_dump(ClassB::$propX);
var_dump(ClassC::$propX);

echo "===SetA===\n";
ClassA::$propX = '2A';
var_dump(ClassA::$propX);
var_dump(ClassB::$propX);
var_dump(ClassC::$propX);

echo "===SetB===\n";
ClassB::$propX = 'B2';
var_dump(ClassA::$propX);
var_dump(ClassB::$propX);
var_dump(ClassC::$propX);

echo "===SetC===\n";
ClassC::$propX = '2C';
var_dump(ClassA::$propX);
var_dump(ClassB::$propX);
var_dump(ClassC::$propX);

?>
===DONE===
--EXPECT--
===INIT===
string(1) "A"
string(1) "C"
string(1) "C"
===SetA===
string(2) "2A"
string(1) "C"
string(1) "C"
===SetB===
string(2) "2A"
string(2) "B2"
string(2) "B2"
===SetC===
string(2) "2A"
string(2) "2C"
string(2) "2C"
===DONE===
