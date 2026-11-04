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
a=[]; a[0] ||= "bar";
    p(a[0])
    h={}; h["foo"] ||= "bar";
    p(h["foo"])

    aa = 5
    aa ||= 25
    p(aa)
    bb ||= 25
    p(bb)
    cc &&=33
    p(cc)
    cc = 5
    cc &&=44
    p(cc)