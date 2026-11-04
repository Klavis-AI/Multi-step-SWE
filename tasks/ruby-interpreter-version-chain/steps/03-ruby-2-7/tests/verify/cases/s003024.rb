class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
module ModuleSpecs
  class ClassWithFoo
    def foo; "foo" end
  end

  class ClassWithSuperFoo
    def foo; [:C] end
  end

  module PrependedModule
    def foo; "foo from prepended module"; end
  end

  module IncludedModule
    def foo; "foo from included module"; end
  end

  def self.build_refined_class(for_super: false)
    if for_super
      Class.new(ClassWithSuperFoo)
    else
      Class.new(ClassWithFoo)
    end
  end
end

refinement = Module.new do
      refine(Enumerable) do
        def foo?
          self.any? ? "yes" : "no"
        end
      end
    end

    foo = Class.new do
      using refinement

      def initialize(items)
        @items = items
      end

      def result
        @items.foo?
      end
    end

    p(foo.new([]).result)
    p(foo.new([1]).result)