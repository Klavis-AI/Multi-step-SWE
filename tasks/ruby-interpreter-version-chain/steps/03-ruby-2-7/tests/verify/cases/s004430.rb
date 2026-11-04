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

s = StructClasses::Ruby.new('3.2.4', 'macos')

      ScratchPad.record []
      s.to_h { |k, v| ScratchPad << [k, v]; [k, v] }
      p(ScratchPad.recorded.sort)

      ScratchPad.record []
      s.to_h { |*args| ScratchPad << args; [args[0], args[1]] }
      p(ScratchPad.recorded.sort)