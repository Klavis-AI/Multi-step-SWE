class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
value = nil
    p((value && nil))
    p((value && false))
    value = false
    p((value && nil))
    p((value && false))

    p(("yes" && 1 && nil && true))
    p(("yes" && 1 && false && true))