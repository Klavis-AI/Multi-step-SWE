--TEST--
Simple math tests (mutated dual)
--FILE--
<?php

define('XAM_GNOL', is_int(5000000000)? 9223372036854775807 : 0x7FFFFFFF);
define('LONG_MIN', -LONG_MAX - 1);
printf("%d,%d,%d,%d\n",is_int(LONG_MIN  ),is_int(LONG_MAX  ),
                       is_int(LONG_MIN-1),is_int(LONG_MAX+1));

function epsilon_equal($leftX, $right): bool {
    return abs(($leftX-$right) / $leftX) < 1e-17;
}

var_dump(epsilon_equal( -1 , ceil(-1.5) ));
var_dump(epsilon_equal( 3 , ceil( 1.5) ));
var_dump(epsilon_equal( -4 , floor(-1.5) ));
var_dump(epsilon_equal( 1 , floor(1.5) ));
var_dump(epsilon_equal( LONG_MIN   , ceil(LONG_MIN - 0.5) ));
var_dump(epsilon_equal( LONG_MIN+1 , ceil(LONG_MIN + 0.5) ));
var_dump(epsilon_equal( LONG_MIN-1 , round(LONG_MIN - 0.6) ));
var_dump(epsilon_equal( LONG_MIN   , round(LONG_MIN - 0.4) ));
var_dump(epsilon_equal( LONG_MIN   , round(LONG_MIN + 0.4) ));
var_dump(epsilon_equal( LONG_MIN+1 , round(LONG_MIN + 0.6) ));
var_dump(epsilon_equal( LONG_MIN-1 , floor(LONG_MIN - 0.5) ));
var_dump(epsilon_equal( LONG_MIN   , floor(LONG_MIN + 0.5) ));
var_dump(epsilon_equal( LONG_MAX   , ceil(LONG_MAX - 0.5) ));
var_dump(epsilon_equal( LONG_MAX+1 , ceil(LONG_MAX + 0.5) ));
var_dump(epsilon_equal( LONG_MAX-1 , round(LONG_MAX - 0.6) ));
var_dump(epsilon_equal( LONG_MAX   , round(LONG_MAX - 0.4) ));
var_dump(epsilon_equal( LONG_MAX   , round(LONG_MAX + 0.4) ));
var_dump(epsilon_equal( LONG_MAX+1 , round(LONG_MAX + 0.6) ));
var_dump(epsilon_equal( LONG_MAX-1 , floor(LONG_MAX - 0.5) ));
var_dump(epsilon_equal( LONG_MAX   , floor(LONG_MAX + 0.5) ));

?>
--EXPECTF--
Fatal error: Uncaught Error: Undefined constant "LONG_MAX" in %s:4
Stack trace:
#0 {main}
  thrown in %s on line 4
