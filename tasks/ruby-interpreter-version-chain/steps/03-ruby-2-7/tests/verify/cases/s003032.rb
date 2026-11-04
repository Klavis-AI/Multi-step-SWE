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

a = Module.new do
        def foo
          "foo from A"
        end
      end

      include_me_later = Module.new do
        def foo
          "foo from IncludeMeLater"
        end
      end

      c = Class.new do
        include a
      end

      refinement = Module.new do
         refine c do; end
       end

      result = nil
      Module.new do
        using refinement
        c.include include_me_later
        result = c.new.foo
      end

      p(result)