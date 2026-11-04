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

p((10 <= 7 ^ 7))
    p((10 < 7 ^ 7))
    p((10 > 7 ^ 7))
    p((10 >= 7 ^ 7))
    p((10 <= 7 | 7))
    p((10 < 7 | 7))
    p((10 > 7 | 7))
    p((10 >= 7 | 7))