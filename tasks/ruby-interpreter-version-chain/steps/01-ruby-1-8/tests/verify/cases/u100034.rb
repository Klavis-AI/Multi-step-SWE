@cls = Array
a = @cls[1, 2, 3, 1, 2]
    p(a.count)
    p(a.count(1))
    p(a.count {|x| x % 2 == 1 })
    p(a.count(1) {|x| x % 2 == 1 })
    begin; a.count(0, 1)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end