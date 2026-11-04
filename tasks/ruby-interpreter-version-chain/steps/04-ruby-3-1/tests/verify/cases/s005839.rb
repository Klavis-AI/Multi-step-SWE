class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
ScratchPad.record []
p(case [0, [2, 4, 6], [3, 9, 27], [4, 16, 64]]
      in [*pre, [*, 9, a], *post]
        [pre, post, a]
      else
        false
      end)