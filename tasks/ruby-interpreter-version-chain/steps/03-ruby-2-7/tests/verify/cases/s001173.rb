class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
@e = (1..5).to_a.to_enum
p(@e.next)
    p(@e.peek)
    p(@e.next)