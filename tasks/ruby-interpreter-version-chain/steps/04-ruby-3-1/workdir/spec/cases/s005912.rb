class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
ScratchPad.record []
p(case {a: 1, b: 2}
      in {a: 1, b: 2, **nil}
        true
      end)

      p(case {a: 1, b: 2}
      in {a: 1, **nil}
        true
      else
        false
      end)