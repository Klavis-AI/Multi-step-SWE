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

p((2-3-4))
    p((4-3+2))

    binary_plus = Class.new(String) do
      alias_method :plus, :+
      def +(a)
        plus(a) + "!"
      end
    end
    s = binary_plus.new("a")

    p((s+s+s))
    p((s+s+s))