@cls = Array
a = @cls[1, 2, 3]
    b = a.clear
    p(a)
    p(b)
    p(b.__id__)