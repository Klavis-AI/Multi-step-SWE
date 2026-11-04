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
f = lambda {|r,| p(r)}
    f.call([], *[])

    f = lambda {|r,*l| p(r); p(l)}
    f.call([], *[1])

    f = lambda{|x| x}
    p(f.call(42))
    p(f.call([42]))
    p(f.call([[42]]))
    p(f.call([42,55]))

    f = lambda{|x,| x}
    p(f.call(42))
    p(f.call([42]))
    p(f.call([[42]]))
    p(f.call([42,55]))

    f = lambda{|*x| x}
    p(f.call(42))
    p(f.call([42]))
    p(f.call([[42]]))
    p(f.call([42,55]))
    p(f.call(42,55))