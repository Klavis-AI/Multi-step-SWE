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
def self.a
      -> do
        p = Proc.new{return :a}
        p.call
      end.call
    end
    p(a)

    def self.b
      lambda do
        p = Proc.new{return :b}
        p.call
      end.call
    end
    p(b)