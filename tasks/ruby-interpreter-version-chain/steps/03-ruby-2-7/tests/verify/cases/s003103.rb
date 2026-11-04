class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
refinement = Module.new do
        refine String do
          def foo; "foo from refinement"; end
        end
      end

      c = Class.new do
        using refinement

        def initialize
          @a = +"1703"

          @a.instance_eval do
            def abc
              "#{self}: #{self.foo}"
            end
          end
        end

        def abc
          @a.abc
        end
      end

      p(c.new.abc)