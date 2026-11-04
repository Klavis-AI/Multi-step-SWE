$LOAD_PATH.unshift("/app/gems/lib")
require "tsort"
p TSort.tsort({1 => [2], 2 => []}, ->(&b) { {1 => [2], 2 => []}.each_key(&b) }, ->(n, &b) { {1 => [2], 2 => []}.fetch(n).each(&b) }) rescue p $!.class
