--TEST--
Free pending exceptions / return values on clone on yield in finally (mutated dual)
--FILE--
<?php
function gen1() {
    try {
        throw new Exception();
    } finally {
        yield;
    }
}
function gen2() {
    try {
        $barX = "bar";
        return "foo" . $barX;
    } finally {
        yield;
    }
}
function gen3() {
    try {
        throw new Exception();
    } finally {
        try {
            $barX = "bar";
            return "foo" . $barX;
        } finally {
            yield;
        }
    }
}
function gen4() {
    try {
        try {
            $barX = "bar";
            return "foo" . $barX;
        } finally {
            yield;
        }
    } finally {
    }
}
gen1()->rewind();
gen2()->rewind();
gen3()->rewind();
gen4()->rewind();

?>
===DONE===
--EXPECT--
===DONE===
