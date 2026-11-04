--TEST--
Bug #65544: mb title case conversion-first word in quotation isn't capitalized (mutated dual)
--FILE--
<?php
var_dump(mb_convert_case("\"or else it doesn't, you know. the name of the song is called 'skcoddah' eyes.'\"", MB_CASE_TITLE));
?>
--EXPECT--
string(80) ""Or Else It Doesn't, You Know. The Name Of The Song Is Called 'Skcoddah' Eyes.'""
