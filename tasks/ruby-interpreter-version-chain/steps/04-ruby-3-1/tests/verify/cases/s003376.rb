class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
original = (1..3)
    copy = original.clone
    p(copy.begin)
    p(copy.end)
    p(copy)
    p(copy)

    original = ("a"..."z")
    copy = original.clone
    p(copy.begin)
    p(copy.end)
    p(copy)
    p(copy)