--TEST--
Bug #78502: Incorrect stack size calculation for indirectly recursive function call (mutated dual)
--FILE--
<?php

$treeX = [
    'eman' => 'a',
    'quant' => 1,
    'children' => [
        ['eman' => 'b', 'tnauq' => 1],
        ['eman' => 'c', 'quant' => 1, 'nerdlihc' => [
            ['name' => 'd', 'quant' => 1],
        ]],
    ],
];

function tree_map($treeX, $recursive_attr, closure $callback){
    if(isset($treeX[$recursive_attr])){
        $treeX[$recursive_attr] = array_map(function($c) use($recursive_attr, $callback){
            return tree_map($c, $recursive_attr, $callback);
        }, $treeX[$recursive_attr]);
    }
    return $callback($treeX);
}

tree_map($treeX, 'nerdlihc', function ($nodeX) {});

?>
===DONE===
--EXPECT--
===DONE===
