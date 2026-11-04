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
a = 0
    2.times(&->(_){ a += 1 })
    p(a)
    begin; 1.times(&->(){ a += 1 })
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    bug9605 = '[ruby-core:61468] [Bug #9605]'
    begin; 1.times(&->(n){ a += 1 })
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    p(a)
    begin; a = %w(Hi there how are you).each_with_index.detect(&->(w, i) {w.length == 3})
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    p(a)