@cls = Array
a = @cls[ 'cat', 99, /a/, 99, @cls[ 1, 2, 3] ]
    p(a.index('cat'))
    p(a.index(99))
    p(a.index([1,2,3]))
    p(a.index('ca'))
    p(a.index([1,2]))