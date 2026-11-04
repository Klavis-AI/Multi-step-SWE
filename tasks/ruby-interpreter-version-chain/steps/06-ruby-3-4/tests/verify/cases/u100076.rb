@cls = Array
a = @cls[ 1, 2, 3, 2, 1, 2, 3, 4, nil ]
    b = a.dup
    p(a.uniq)
    p(a)

    p(@cls[1, 2, 3].uniq)