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

p((false && 2 <=> 3))
    p((false && 3 == false))
    p((false && 3 === false))
    p((false && 3 != true))

    class FalseClass; def =~(o); o == false; end; end
    p((false && true =~ false))
    p((false && true =~ false))
    class FalseClass; undef_method :=~; end

    p((false && true !~ true))