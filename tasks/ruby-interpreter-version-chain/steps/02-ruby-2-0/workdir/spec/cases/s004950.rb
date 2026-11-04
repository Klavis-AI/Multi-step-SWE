class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
@calls = []
    def foo; @calls << :foo; end
    def bar; @calls << :bar; end

    case true
    when foo, bar;
    end

    p(@calls)