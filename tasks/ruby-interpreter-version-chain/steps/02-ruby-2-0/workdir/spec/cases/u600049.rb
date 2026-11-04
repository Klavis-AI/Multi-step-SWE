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
bug2050 = '[ruby-core:25629]'
    a = Hash.new {[]}
    b = [1, 2]
    p(a[:x] += [*b, 3])
    p(a[:x])
    p(a[:x] <<= [*b, 3])
    p(a[:x])