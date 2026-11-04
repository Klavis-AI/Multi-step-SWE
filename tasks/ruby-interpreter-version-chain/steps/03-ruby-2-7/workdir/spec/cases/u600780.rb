# frozen_string_literal: false
def foo
    assert_equal(nil, ->(&b){ b }.call)
  end
def return_in_current(val)
    1.tap(&->(*) {return 0})
    val
  end
def yield_block
    yield
  end
def return_in_callee(val)
    yield_block(&->(*) {return 0})
    val
  end
def break_in_current(val)
    1.tap(&->(*) {break 0})
    val
  end
def break_in_callee(val)
    yield_block(&->(*) {break 0})
    val
  end
p(->(*a){ a }.call(1,2))
    p(->(a,b,*c){ [a,b,c] }.call(1,2))
    begin; ->(a,*b){ }.call()
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end