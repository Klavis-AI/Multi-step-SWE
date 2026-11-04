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

p((2*1/2))
    # TODO: Make these specs not rely on specific behaviour / result values
    # by using mocks.
    p((2*1/2))

    p((10/7/5))
    p((10/7/5))

    p((101 % 55 % 7))
    p((101 % 55 % 7))

    p((50*20/7%42))
    p((50*20/7%42))