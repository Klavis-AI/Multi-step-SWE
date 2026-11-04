class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
a = []
    -> {
      a << 1
      redo if a.size < 2
      a << 2
    }.call
    p(a)