@cls = Array
a = @cls[ 1, 2, 3 ]
    a_id = a.__id__
    p(a.to_a)
    p(a.to_a.__id__)