class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
value = nil
    p((value and nil))
    p((value and false))
    value = false
    p((value and nil))
    p((value and false))

    p(("yes" and 1 and nil and true))
    p(("yes" and 1 and false and true))