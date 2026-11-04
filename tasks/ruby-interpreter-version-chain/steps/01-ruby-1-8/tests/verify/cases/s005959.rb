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

p((1 if 2 and 3))
    p((1 if 2 or 3))

    p((1 unless false and true))
    p((1 unless false or false))

    p((1 while true and false))    # would hang upon error
    p((1 while false or false))

    p(((raise until true and false) rescue 10))
    p((1 until false or true))