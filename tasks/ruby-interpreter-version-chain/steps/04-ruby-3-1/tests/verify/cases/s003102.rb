class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
refinement = Module.new do
        refine Integer do
          def foo; "foo from refinement"; end
        end
      end

      c = Class.new do
        using refinement

        def abc
          block = -> {
            1.foo
          }

          self.instance_exec(&block)
        end
      end

      p(c.new.abc)