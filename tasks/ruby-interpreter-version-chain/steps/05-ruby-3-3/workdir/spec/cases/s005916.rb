class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
ScratchPad.record []
p(case {}
      in {**nil}
        true
      else
        false
      end)

      p(case {a: 1}
      in {**nil}
        true
      else
        false
      end)