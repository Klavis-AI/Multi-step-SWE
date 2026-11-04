class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
@hash = { a: 1, b: 2, c: 3 }
ScratchPad.record []
    klass = Class.new(Hash) do
      def [](value)
        ScratchPad << :used_subclassed_operator
        super
      end
    end

    h = klass.new
    h[:bar] = 12
    h[:foo] = 42
    h.slice(:foo)

    p(ScratchPad.recorded)