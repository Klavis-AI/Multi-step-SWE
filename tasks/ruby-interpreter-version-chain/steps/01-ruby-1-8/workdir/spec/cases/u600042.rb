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
*a = *[]; p(a)
    *a = *[1]; p(a)
    *a = *[nil]; p(a)
    *a = *[[]]; p(a)
    *a = *[1,2]; p(a)
    *a = *[*[]]; p(a)
    *a = *[*[1]]; p(a)
    *a = *[*[1,2]]; p(a)