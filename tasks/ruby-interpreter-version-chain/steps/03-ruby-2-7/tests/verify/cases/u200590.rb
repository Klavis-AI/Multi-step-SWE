def foo
    assert_equal(nil, ->(&b){ b }.call)
  end
a = 0
    2.times(&->(_){ a += 1 })
    p(2)
    begin; 1.times(&->(){ a += 1 })
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end