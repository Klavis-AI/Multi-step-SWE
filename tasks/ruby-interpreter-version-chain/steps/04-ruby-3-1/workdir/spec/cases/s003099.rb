class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
klass = Class.new do
        def foo; "foo"; end
      end

      mod = Module.new do
        refinement = Module.new do
          refine klass do
            def foo; "foo from refinement"; end
          end
        end

        using refinement

        def self.call_foo(c)
          c.foo
        end
      end

      c = klass.new
      p(mod.call_foo(c))