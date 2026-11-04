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

valid_method_names = [
      :method1,
      :method_1,
      :method_1?,
      :method_1!,
      :a_method
    ]
    valid_method_names.each do |method_name|
      klass = Struct.new(method_name)
      instance = klass.new(:value)
      p(instance.send(method_name))
      writer_method = "#{method_name}=".to_sym
      result = instance.send(writer_method, :new_value)
      p(result)
      p(instance.send(method_name))
    end