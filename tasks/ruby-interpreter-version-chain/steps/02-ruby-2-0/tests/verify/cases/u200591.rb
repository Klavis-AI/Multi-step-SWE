def foo
    assert_equal(nil, ->(&b){ b }.call)
  end
p(->(*a){ a }.call(1,2))
    p(->(a,b,*c){ [a,b,c] }.call(1,2))
    begin; ->(a,*b){ }.call()
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end