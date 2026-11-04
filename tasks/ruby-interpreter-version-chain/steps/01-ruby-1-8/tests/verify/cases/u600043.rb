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
a,b,*c = nil; p([a,b,c])
    a,b,*c = 1; p([a,b,c])
    a,b,*c = []; p([a,b,c])
    a,b,*c = [1]; p([a,b,c])
    a,b,*c = [nil]; p([a,b,c])
    a,b,*c = [[]]; p([a,b,c])
    a,b,*c = [1,2]; p([a,b,c])
    a,b,*c = [*[]]; p([a,b,c])
    a,b,*c = [*[1]]; p([a,b,c])
    a,b,*c = [*[1,2]]; p([a,b,c])