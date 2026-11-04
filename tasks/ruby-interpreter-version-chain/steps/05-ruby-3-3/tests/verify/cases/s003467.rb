class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((0..2).overlap?(..3))
    p((0..2).overlap?(..1))
    p((0..2).overlap?(..0))

    p((..3).overlap?(0..2))
    p((..1).overlap?(0..2))
    p((..0).overlap?(0..2))

    p((0..2).overlap?(-1..))
    p((0..2).overlap?(1..))
    p((0..2).overlap?(2..))

    p((-1..).overlap?(0..2))
    p((1..).overlap?(0..2))
    p((2..).overlap?(0..2))

    p((0..).overlap?(2..))
    p((..0).overlap?(..2))

    p((nil..nil).overlap?(4..6))
    p((4..6).overlap?(nil..nil))

    ruby_version_is "4.0" do
      p((nil..nil).overlap?(..6))
      p((..6).overlap?(nil..nil))
    end

    p((nil..nil).overlap?(4..))
    p((4..).overlap?(nil..nil))

    p((..10).overlap?(10..))
    p((10..).overlap?(..10))