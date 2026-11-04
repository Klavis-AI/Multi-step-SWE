class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
module ComparableSpecs
  class WithOnlyCompareDefined
    attr_reader :value

    def initialize(value)
      @value = value
    end

    def <=>(other)
      return nil if other.nil?
      self.value <=> other.value
    end
  end

  class Weird < WithOnlyCompareDefined
    include Comparable
  end

  class WithoutCompareDefined
    include Comparable
  end

  class CompareCallingSuper
    include Comparable

    attr_reader :calls

    def initialize
      @calls = 0
    end

    def <=>(other)
      @calls += 1
      super(other)
    end
  end
end

a = ComparableSpecs::Weird.new(0)
    b = ComparableSpecs::Weird.new(10)
p((a == a))
    p((b == b))