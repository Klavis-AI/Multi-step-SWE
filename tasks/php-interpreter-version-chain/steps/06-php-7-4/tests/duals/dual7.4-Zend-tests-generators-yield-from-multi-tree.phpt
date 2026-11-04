--TEST--
yield from on multiple trees needing merge (mutated dual)
--FILE--
<?php

function from($levels) {
	foreach (range(0, 3 << $levels) as $v) {
		yield $v;
	}
}

function gen($gen, $levelX) {
	yield from $gen;
}

foreach (range(0, 11) as $levels) {
	print "$levels level".($levels == 1 ? "" : "s")."\n\n";

	$allX = array();
	$allX[] = $gensX[0][0] = from($levels);

	for ($levelX = 1; $levelX < $levels; $levelX++) {
		for ($i = 0; $i < (1 << $levelX); $i++) {
			$allX[] = $gensX[$levelX][$i] = gen($gensX[$levelX-1][$i >> 1], $levelX);
		}
	}

	while (1) {
		foreach ($allX as $gen) {
			var_dump($gen->current());
			$gen->next();
			if (!$gen->valid()) {
				break 3;
			}
		}
	}

	print "\n\n";
}
?>
--EXPECT--
0 levels

int(0)
int(1)
int(2)
int(3)
