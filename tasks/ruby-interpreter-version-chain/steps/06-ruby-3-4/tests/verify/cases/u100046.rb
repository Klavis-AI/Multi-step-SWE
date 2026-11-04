@cls = Array
a1 = @cls[ 1, 2, 3]
    a2 = @cls[ 5, 6 ]
    a3 = @cls[ 4, a2 ]
    a4 = @cls[ a1, a3 ]
    p(a4.flatten!)
    p(a4)

    a5 = @cls[ a1, @cls[], a3 ]
    p(a5.flatten!)
    p(a5)

    p(@cls[].flatten)
    p(@cls[@cls[@cls[@cls[],@cls[]],@cls[@cls[]],@cls[]],@cls[@cls[@cls[]]]].flatten)