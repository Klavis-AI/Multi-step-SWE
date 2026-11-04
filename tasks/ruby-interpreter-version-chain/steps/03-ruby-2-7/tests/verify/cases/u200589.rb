def foo
    assert_equal(nil, ->(&b){ b }.call)
  end
p(lambda{|a| a}.call(1))
    p(lambda{|a, b| [a,b]}.call(1,2))
    begin; lambda{|a|}.call(1,2)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; lambda{|a|}.call()
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; lambda{}.call(1)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; lambda{|a, b|}.call(1,2,3)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    p(->(a){ a }.call(1))
    p(->(a,b){ [a,b] }.call(1,2))
    begin; ->(a){ }.call(1,2)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; ->(a){ }.call()
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; ->(){ }.call(1)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; ->(a,b){ }.call(1,2,3)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end