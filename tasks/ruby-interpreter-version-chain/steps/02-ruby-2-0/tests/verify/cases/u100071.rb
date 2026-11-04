@cls = Array
a = @cls[ 4, 1, 2, 3 ]
    p(a.sort)
    p(a)

    p(a.sort { |x, y| y <=> x})
    p(a)

    a.fill(1)
    p(a.sort)
    
    p(@cls[].sort)