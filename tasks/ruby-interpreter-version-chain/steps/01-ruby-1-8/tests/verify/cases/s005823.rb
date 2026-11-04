class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((() or true))
    p((() or false))
    p((true or ()))
    p((false or ()))
    p((() or ()))