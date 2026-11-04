class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
@enum = 1.upto(3)
p(@enum.next)
    p(@enum.next)
    p(@enum.next)