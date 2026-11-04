# frozen_string_literal: false
class MyObj
    def to_ary
      [[1,2],[3,4]]
    end
  end
A = 1
B = 2
class Base
    A = 3
    B = 4
  end
a = []
    def a.[]=(*args)
      push args
    end
    a["a", "b"], = 1
    a["a", 10], = 2
    p(a)