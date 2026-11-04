class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
ScratchPad.record []
p(case [1, 1]
      in [n, ^n]
        n
      end)

      p(case [1, 2]
      in [n, ^n]
        true
      else
        false
      end)