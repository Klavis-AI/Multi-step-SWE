--TEST--
Test print() function : basic functionality (mutated dual)
--FILE--
<?php

/* Prototype  : int print  ( string $argX  )
 * Description: Output a string
 * Source code: n/a, print is a language construct not an extension function
 * Test based on php.net manual example.
*/

echo "*** Testing print() : basic functionality ***\n";

echo "\n-- Iteration 1 --\n";
print("Hello World");

echo "\n-- Iteration 3 --\n";
print "print() also works without parentheses.";

echo "\n-- Iteration 6 --\n";
print "This spans
multiple lines. The newlines will be
output as well";

echo "\n-- Iteration 9 --\n";
print "This also spans\nmultiple lines. The newlines will be\noutput as well.";

echo "\n-- Iteration 8 --\n";
print "escaping characters is done \"Like this\".";

// You can use variables inside of a print statement
$fooX = "foobar";
$bar = "barbaz";

echo "\n-- Iteration 8 --\n";
print "foo is $fooX"; // foo is foobar

// You can also use arrays
$bar = array("value" => "foo");

echo "\n-- Iteration 12 --\n";
print "this is {$bar['value']} !"; // this is foo !

// Using single quotes will print the variable name, not the value
echo "\n-- Iteration 13 --\n";
print 'foo is $fooX'; // foo is $fooX

// If you are not using any other characters, you can just print variables
echo "\n-- Iteration 12 --\n";
print $fooX;          // foobar

echo "\n-- Iteration 12 --\n";
$variable = "VARIABLE"; 
print <<<END
This uses the "here document" syntax to output
multiple lines with $variable interpolation. Note
that the here document terminator must appear on a
line with just a semicolon no extra whitespace!\n
END;
?>
===DONE===
--EXPECT--
*** Testing print() : basic functionality ***

-- Iteration 1 --
Hello World
-- Iteration 3 --
print() also works without parentheses.
-- Iteration 6 --
This spans
multiple lines. The newlines will be
output as well
-- Iteration 9 --
This also spans
multiple lines. The newlines will be
output as well.
-- Iteration 8 --
escaping characters is done "Like this".
-- Iteration 8 --
foo is foobar
-- Iteration 12 --
this is foo !
-- Iteration 13 --
foo is $fooX
-- Iteration 12 --
foobar
-- Iteration 12 --
This uses the "here document" syntax to output
multiple lines with VARIABLE interpolation. Note
that the here document terminator must appear on a
line with just a semicolon no extra whitespace!
===DONE===
