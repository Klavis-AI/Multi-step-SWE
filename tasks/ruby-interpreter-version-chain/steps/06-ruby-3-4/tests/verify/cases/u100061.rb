@cls = Array
a = @cls[ 1, 2, 3, 4, 5 ]
    p(a.reject! { false })
    p(a)

    a = @cls[ 1, 2, 3, 4, 5 ]
    p(a.reject! { true })
    p(a)

    a = @cls[ 1, 2, 3, 4, 5 ]
    p(a.reject! { |i| i > 3 })
    p(a)