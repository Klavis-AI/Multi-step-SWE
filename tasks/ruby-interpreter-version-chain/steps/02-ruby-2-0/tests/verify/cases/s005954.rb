class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
module PrecedenceSpecs
  class NonUnaryOpTest
    def add_num(arg)
      [1].collect { |i| arg + i +1 }
    end
    def sub_num(arg)
      [1].collect { |i| arg + i -1 }
    end
    def add_str
      %w[1].collect { |i| i +'1' }
    end
    def add_var
      [1].collect { |i| i +i }
    end
  end
end

a = b = 10
    p(a)
    p(b)

    a = b = 10
    a %= b %= 3
    p(a)
    p(b)

    a = b = 10
    a /= b /= 2
    p(a)
    p(b)

    a = b = 10
    a -= b -= 2
    p(a)
    p(b)

    a = b = 10
    a += b += 2
    p(a)
    p(b)

    a,b = 32,64
    a |= b |= 2
    p(a)
    p(b)

    a,b = 25,13
    a &= b &= 7
    p(a)
    p(b)

    a,b=8,2
    a >>= b >>= 1
    p(a)
    p(b)

    a,b=8,2
    a <<= b <<= 1
    p(a)
    p(b)

    a,b=8,2
    a *= b *= 2
    p(a)
    p(b)

    a,b=10,20
    a &&= b &&= false
    p(a)
    p(b)

    a,b=nil,nil
    a ||= b ||= 10
    p(a)
    p(b)

    a,b=2,3
    a **= b **= 2
    p(a)
    p(b)