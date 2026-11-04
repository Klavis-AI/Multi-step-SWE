class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
module StructClasses

  class Apple < Struct; end

  Ruby = Struct.new(:version, :platform)
  Single = Struct.new(:value)

  Car = Struct.new(:make, :model, :year)

  class Honda < Car
    def initialize(*args)
      self.make = "Honda"
      super(*args)
    end
  end

  class StructWithOverriddenName < Struct.new(:a)
    def self.name
      "A"
    end
  end

  class SubclassX < Struct
  end

  class SubclassX
    attr_reader :key
    def initialize(*)
      @key = :value
      super
    end
  end

  class StructSubclass < Struct; end
end

klass = Struct.new(:'current-state')
    tuple = klass.new(0)
    p(tuple['current-state'])
    p(tuple[:'current-state'])
    p(tuple[0])