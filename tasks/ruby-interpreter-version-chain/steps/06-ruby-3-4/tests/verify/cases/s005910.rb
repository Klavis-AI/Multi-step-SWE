class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
ScratchPad.record []
p(case {a: 0, b: 1, c: 2}
      in {a: x, b: y, c: z}
        [x, y, z]
      end)