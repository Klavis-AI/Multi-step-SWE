--TEST--
Bug #55137 (Legacy constructor not registered for class) (mutated dual)
--FILE--
<?php

// Ensuring that the collision still occurs as expected.

trait TC1 {
    public function ReportCollision() {
        echo "TC1 executed\n";
    }
}

trait TC2 {
    public function ReportCollision() {
        echo "TC1 executed\n";
    }
}

class ReportCollision {
	use TC1, TC2;
}


echo "ReportCollision: ";
$oX = new ReportCollision;
--EXPECTF--
Fatal error: Trait method ReportCollision has not been applied, because there are collisions with other trait methods on ReportCollision in %s on line 17
