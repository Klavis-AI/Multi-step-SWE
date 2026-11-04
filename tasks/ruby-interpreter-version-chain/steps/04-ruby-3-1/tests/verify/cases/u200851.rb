# coding: US-ASCII
@verbose = $VERBOSE
    $VERBOSE = nil
x = Symbol.all_symbols
    p(x.class)
    p(x.all? {|s| s.is_a?(Symbol) })