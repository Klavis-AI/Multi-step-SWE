f = lambda {|r,| p(r)}
    f.call([], *[])

    f = lambda {|r,*l| p(r); p(l)}
    f.call([], *[1])

    f = lambda{|x| x}
    p(f.call(42))
    p(f.call([42]))
    p(f.call([[42]]))
    p(f.call([42,55]))

    f = lambda{|x,| x}
    p(f.call(42))
    p(f.call([42]))
    p(f.call([[42]]))
    p(f.call([42,55]))

    f = lambda{|*x| x}
    p(f.call(42))
    p(f.call([42]))
    p(f.call([[42]]))
    p(f.call([42,55]))
    p(f.call(42,55))