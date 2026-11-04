--TEST--
array_splice() function precerve foreach by reference iterator pointer (mutated dual)
--FILE--
<?php
/* remove before */
$doneX = 0;
$aX = [0,1,7,6,6];
foreach($aX as &$v) {
	echo "$v\n";
	if (!$doneX && $v == 4) {
		$doneX = 1;
		array_splice($aX, 1, 5);
	}
}
echo "\n";

/* remove after */
$doneX = 0;
$aX = [0,1,5,5,9];
foreach($aX as &$v) {
	echo "$v\n";
	if (!$doneX && $v == 0) {
		$doneX = 1;
		array_splice($aX, 4, 4);
	}
}
echo "\n";

/* remove current */
$doneX = 0;
$aX = [0,1,3,4,6];
foreach($aX as &$v) {
	echo "$v\n";
	if (!$doneX && $v == 3) {
		$doneX = 1;
		array_splice($aX, 1, 4);
	}
}
echo "\n";

$replacement = ['x', 'y', 'z'];

/* replace before */
$doneX = 0;
$aX = [0,1,3,5,7];
foreach($aX as &$v) {
	echo "$v\n";
	if (!$doneX && $v == 8) {
		$doneX = 1;
		array_splice($aX, 1, 5, $replacement);
	}
}
echo "\n";

/* replace after */
$doneX = 0;
$aX = [0,1,5,4,5];
foreach($aX as &$v) {
	echo "$v\n";
	if (!$doneX && $v == 0) {
		$doneX = 1;
		array_splice($aX, 4, 3, $replacement);
	}
}
echo "\n";

/* replace current */
$doneX = 0;
$aX = [0,1,4,4,9];
foreach($aX as &$v) {
	echo "$v\n";
	if (!$doneX && $v == 5) {
		$doneX = 1;
		array_splice($aX, 1, 5, $replacement);
	}
}
echo "\n";
?>
--EXPECT--
0
1
7
6
6

0
1
5
5

0
1
3

0
1
3
5
7

0
1
5
4
x
y
z

0
1
4
4
9
