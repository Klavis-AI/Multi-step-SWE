@cls = Array
a = @cls[ 5, 4, 3, 2, 1 ]
    p(a.class)
    p(a.length)
    5.times { |i| p(a[i]) }
    p(a[6])