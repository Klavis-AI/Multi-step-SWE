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
f = ->(a,b,c=3,*d,&e){ [a,b,c,d,e.call(d + [a,b,c])] }
    p(f.call(1,2){|z| z.inject{|s,x| s+x} })
    p(->(&b){ b }.call)
    foo { puts "bogus block " }
    p(->(&b){ b.call }.call { 1 })
    _b = nil
    p(->(&_b){ _b.call }.call { 1 })
    p(_b)