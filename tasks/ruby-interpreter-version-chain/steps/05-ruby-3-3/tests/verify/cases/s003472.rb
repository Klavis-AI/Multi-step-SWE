class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((0...2).overlap?(2..4))
    p((2..4).overlap?(0...2))

    p(('a'...'c').overlap?('c'..'e'))
    p(('c'..'e').overlap?('a'...'c'))

    p((0...10).overlap?(6..10))
    p((nil...6).overlap?(4..6))
    p((nil...10).overlap?(nil..10))

    p((nil...4).overlap?(4..10))
    p((nil...10).overlap?(10..))
    p((10..).overlap?(nil...10))
    p((6..).overlap?(0...6))