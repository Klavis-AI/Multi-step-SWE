class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((0..2).overlap?(..-1))
    p((0..2).overlap?(3..))

    p((..-1).overlap?(0..2))
    p((3..).overlap?(0..2))

    p((..0).overlap?(10..))