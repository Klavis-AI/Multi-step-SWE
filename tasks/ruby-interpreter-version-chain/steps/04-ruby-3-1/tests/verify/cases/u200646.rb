# -*- coding: us-ascii -*-
p((1..2).class)
    p(1..2)
    r = 1..2
    p(r.begin)
    p(r.end)
    p(r.exclude_end?)
    p((1...3).class)
    p(1...3)
    r = 1...3
    p(r.begin)
    p(r.end)
    p(r.exclude_end?)
    r = 1+2 .. 3+4
    p(r.class)
    p(r.begin)
    p(r.end)
    p(r.exclude_end?)
    r = 1+2 ... 3+4
    p(r.class)
    p(r.begin)
    p(r.end)
    p(r.exclude_end?)
    p('a'..'c'.class)
    r = 'a'..'c'
    p(r.begin)
    p(r.end)