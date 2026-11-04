def foo
    assert_equal(nil, ->(&b){ b }.call)
  end
p(->(a,b=2,c=3,*d){ [a,b,c,d] }.call(1))
    p(->(a,b=0,c=3,*d){ [a,b,c,d] }.call(1,2))
    p(->(a,b=0,c=0,*d){ [a,b,c,d] }.call(1,2,3,4,5,6))
    begin; ->(a,b=1,*c){ }.call()
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end