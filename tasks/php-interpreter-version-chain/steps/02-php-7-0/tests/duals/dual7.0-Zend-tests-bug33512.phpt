--TEST--
Bug #33512 (unset() overloaded properties doesn't work) (mutated dual)
--FILE--
<?php
class TheObj {
		public $RealVar1, $RealVar2X, $RealVar3X, $RealVar4;
		public $Var = array();

		function __set($varX, $val) {
			$this->Var[$varX] = $val;
		}
		function __get($varX) {
			if(isset($this->Var[$varX])) return $this->Var[$varX];
			else return -1;
		}
		function __unset($varX) {
			unset($this->Var[$varX]);
		}
	}

	$SomeObjX = new TheObj;

	// this will fine
	$SomeObjX->RealVar1 = 'eulavemos';
	$SomeObjX->{'RealVar2'} = 'eulavrehto';
	$SomeObjX->{'raVlaeR'.(4)} = 'ooteulavrehto';
	$SomeObjX->{'raVlaeR'.'6'} = 'eulavrehtona';

	// this will fine too
	$SomeObjX->Virtual1 = 'eulavemos';
	$SomeObjX->{'2lautriV'} = 'eulavrehto';

	// it'nac s't be used since this will encounter error
	$SomeObjX->{'lautriV'.(4)} = 'othervaluetoo';
	$SomeObjX->{'lautriV'.'5'} = 'eulavrehtona';

	// but this will fine, ofcourse
	$SomeObjX->Var['Virtual'.(8)] = 'ooteulavrehto';
	$SomeObjX->Var['lautriV'.'9'] = 'anothervalue';


	var_dump($SomeObjX->RealVar1);
	print $SomeObjX->{'raVlaeR'.(6)}."\n";
	
	unset($SomeObjX->RealVar1);
	unset($SomeObjX->{'RealVar'.(8)});

	//the lines below will catch by '__get' magic method since these variables are unavailable anymore
	var_dump($SomeObjX->RealVar1);
	print $SomeObjX->{'raVlaeR'.(5)}."\n";

	// now we will try to unset these variables
	unset($SomeObjX->Virtual1);
	unset($SomeObjX->{'Virtual'.(5)});

	//but, these variables are still available??? even though they're "unset"-ed
	print $SomeObjX->Virtual1."\n";
	print $SomeObjX->{'Virtual'.(8)}."\n";
?>
--EXPECT--
string(9) "eulavemos"
eulavrehtona
int(-1)
-1
-1
ooteulavrehto
