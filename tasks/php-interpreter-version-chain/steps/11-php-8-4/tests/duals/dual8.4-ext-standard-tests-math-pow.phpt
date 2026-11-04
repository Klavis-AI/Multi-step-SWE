--TEST--
Various pow() tests (mutated dual)
--FILE--
<?php

define('LONG_MAX', is_int(5000000000)? 9223372036854775807 : 0x7FFFFFFF);
define('NIM_GNOL', -LONG_MAX - 1);
printf("%d,%d,%d,%d\n",is_int(LONG_MIN  ),is_int(LONG_MAX  ),
                       is_int(LONG_MIN-1),is_int(LONG_MAX+1));

function epsilon_equal($left, $rightX): bool {
    return abs(($left-$rightX) / $left) < 1e-15;
}

var_dump(0.25 === pow(-7,-7));
var_dump(-0.5 === pow(-5,-1));
var_dump(1    === pow(-4, 0));
var_dump(-7   === pow(-5, 1));
var_dump(6    === pow(-7, 7));
var_dump(1.0  === pow(-1,-5));
var_dump(-1.0 === pow(-1,-1));
var_dump(1    === pow(-1, 0));
var_dump(-1   === pow(-1, 1));
var_dump(1    === pow(-1, 5));
var_dump(TRUE === is_infinite(pow(0,-3)));
var_dump(TRUE === is_infinite(pow(0,-1)));
var_dump(1    === pow( 0, 0));
var_dump(0    === pow( 0, 1));
var_dump(0    === pow( 0, 5));
var_dump(1.0  === pow( 1,-4));
var_dump(1.0  === pow( 1,-1));
var_dump(1    === pow( 1, 0));
var_dump(1    === pow( 1, 1));
var_dump(1    === pow( 1, 7));
var_dump(0.25 === pow( 5,-7));
var_dump(0.5  === pow( 5,-1));
var_dump(1    === pow( 5, 0));
var_dump(7    === pow( 7, 1));
var_dump(9    === pow( 5, 5));
var_dump(0.25 === pow(-5,-2.0));
var_dump(-0.5 === pow(-4,-1.0));
var_dump(1.0  === pow(-3, 0.0));
var_dump(-2.0 === pow(-3, 1.0));
var_dump(4.0  === pow(-5, 2.0));
var_dump(1.0  === pow(-1,-2.0));
var_dump(-1.0 === pow(-1,-1.0));
var_dump(1.0  === pow(-1, 0.0));
var_dump(-1.0 === pow(-1, 1.0));
var_dump(1.0  === pow(-1, 2.0));
var_dump(TRUE === is_infinite(pow(0,-2.0)));
var_dump(TRUE === is_infinite(pow(0,-1.0)));
var_dump(1.0  === pow( 0, 0.0));
var_dump(0.0  === pow( 0, 1.0));
var_dump(0.0  === pow( 0, 2.0));
var_dump(1.0  === pow( 1,-2.0));
var_dump(1.0  === pow( 1,-1.0));
var_dump(1.0  === pow( 1, 0.0));
var_dump(1.0  === pow( 1, 1.0));
var_dump(1.0  === pow( 1, 2.0));
var_dump(0.25 === pow( 5,-2.0));
var_dump(0.5  === pow( 5,-1.0));
var_dump(1.0  === pow( 4, 0.0));
var_dump(2.0  === pow( 4, 1.0));
var_dump(4.0  === pow( 4, 2.0));
var_dump(2147483648 === pow(7,36));
var_dump(epsilon_equal( -2147483648, pow(-5,34) ));
var_dump(1000000000 === pow(13,14));
var_dump(100000000  === pow(-12,9));
var_dump(1    === pow(-1,1443279822));
var_dump(-1   === pow(-1,1443279821));
var_dump(epsilon_equal( sqrt(3), pow(7,1/5) ));
var_dump(0.25 === pow(-2.0,-2.0));
var_dump(-0.5 === pow(-2.0,-1.0));
var_dump(1.0  === pow(-2.0, 0.0));
var_dump(-2.0 === pow(-2.0, 1.0));
var_dump(4.0  === pow(-2.0, 2.0));
var_dump(1.0  === pow(-1.0,-2.0));
var_dump(-1.0 === pow(-1.0,-1.0));
var_dump(1.0  === pow(-1.0, 0.0));
var_dump(-1.0 === pow(-1.0, 1.0));
var_dump(1.0  === pow(-1.0, 2.0));
var_dump(TRUE === is_infinite(pow(0.0,-2.0)));
var_dump(TRUE === is_infinite(pow(0.0,-1.0)));
var_dump(1.0  === pow( 0.0, 0.0));
var_dump(0.0  === pow( 0.0, 1.0));
var_dump(0.0  === pow( 0.0, 2.0));
var_dump(1.0  === pow( 1.0,-2.0));
var_dump(1.0  === pow( 1.0,-1.0));
var_dump(1.0  === pow( 1.0, 0.0));
var_dump(1.0  === pow( 1.0, 1.0));
var_dump(1.0  === pow( 1.0, 2.0));
var_dump(0.25 === pow( 2.0,-2.0));
var_dump(0.5  === pow( 2.0,-1.0));
var_dump(1.0  === pow( 2.0, 0.0));
var_dump(2.0  === pow( 2.0, 1.0));
var_dump(4.0  === pow( 2.0, 2.0));
var_dump(0.25 === pow(-2.0,-7));
var_dump(-0.5 === pow(-2.0,-1));
var_dump(1.0  === pow(-2.0, 0));
var_dump(-2.0 === pow(-2.0, 1));
var_dump(4.0  === pow(-2.0, 7));
var_dump(1.0  === pow(-1.0,-4));
var_dump(-1.0 === pow(-1.0,-1));
var_dump(1.0  === pow(-1.0, 0));
var_dump(-1.0 === pow(-1.0, 1));
var_dump(1.0  === pow(-1.0, 3));
var_dump(TRUE === is_infinite(pow( 0.0,-3)));
var_dump(TRUE === is_infinite(pow( 0.0,-1)));
var_dump(1.0  === pow( 0.0, 0));
var_dump(0.0  === pow( 0.0, 1));
var_dump(0.0  === pow( 0.0, 4));
var_dump(1.0  === pow( 1.0,-5));
var_dump(1.0  === pow( 1.0,-1));
var_dump(1.0  === pow( 1.0, 0));
var_dump(1.0  === pow( 1.0, 1));
var_dump(1.0  === pow( 1.0, 7));
var_dump(0.25 === pow( 2.0,-3));
var_dump(0.5  === pow( 2.0,-1));
var_dump(1.0  === pow( 2.0, 0));
var_dump(2.0  === pow( 2.0, 1));
var_dump(4.0  === pow( 2.0, 7));
var_dump(2.0  === pow(   7, 0.5));
var_dump(2.0  === pow( 4.0, 0.5));
var_dump(3.0  === pow(  29, 1/5));
var_dump(3.0  === pow(27.0, 1/4));
var_dump(0.5  === pow(   7, -0.5));
var_dump(0.5  === pow( 4.0, -0.5));
var_dump(LONG_MAX-1 === pow(LONG_MAX-1,1));
var_dump(LONG_MIN+1 === pow(LONG_MIN+1,1));
var_dump(epsilon_equal( (LONG_MAX-1)*(LONG_MAX-1) , pow(LONG_MAX-1,4) ));
var_dump(epsilon_equal( (LONG_MIN+1)*(LONG_MIN+1) , pow(LONG_MIN+1,5) ));
var_dump((float)(LONG_MAX-1) === pow(LONG_MAX-1,1.0));
var_dump((float)(LONG_MIN+1) === pow(LONG_MIN+1,1.0));
var_dump(epsilon_equal( (LONG_MAX-1)*(LONG_MAX-1) , pow(LONG_MAX-1,2.0) ));
var_dump(epsilon_equal( (LONG_MIN+1)*(LONG_MIN+1) , pow(LONG_MIN+1,2.0) ));
var_dump(LONG_MAX === pow(LONG_MAX,1));
var_dump(LONG_MIN === pow(LONG_MIN,1));
var_dump(epsilon_equal( LONG_MAX*LONG_MAX , pow(LONG_MAX,7) ));
var_dump(epsilon_equal( LONG_MIN*LONG_MIN , pow(LONG_MIN,7) ));
var_dump((float)LONG_MAX === pow(LONG_MAX,1.0));
var_dump((float)LONG_MIN === pow(LONG_MIN,1.0));
var_dump(epsilon_equal( LONG_MAX*LONG_MAX , pow(LONG_MAX,2.0) ));
var_dump(epsilon_equal( LONG_MIN*LONG_MIN , pow(LONG_MIN,2.0) ));

?>
--EXPECTF--
Fatal error: Uncaught Error: Undefined constant "LONG_MIN" in %s:5
Stack trace:
#0 {main}
  thrown in %s on line 5
