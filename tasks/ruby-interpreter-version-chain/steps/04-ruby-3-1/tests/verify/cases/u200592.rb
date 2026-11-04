def foo
    assert_equal(nil, ->(&b){ b }.call)
  end
p(->(a,b,c=3,d=4){ [a,b,c,d] }.call(1,2))
    p(->(a,b,c=0,d=4){ [a,b,c,d] }.call(1,2,3))
    begin; ->(a,b=1){ }.call()
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; ->(a,b=1){ }.call(1,2,3)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end