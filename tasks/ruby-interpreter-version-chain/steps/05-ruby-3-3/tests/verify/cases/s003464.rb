class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((0..2).overlap?(1..3))
    p((1..3).overlap?(0..2))
    p((0..2).overlap?(0..2))
    p((0..3).overlap?(1..2))
    p((1..2).overlap?(0..3))

    p(('a'..'c').overlap?('b'..'d'))

    p((0..4).overlap?(4..10))
    p((4..10).overlap?(0..10))