class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
ScratchPad.record []
p(case 0
      in 0
        true
      end)

      p(case 0
      in (
        -1..1)
        true
      end)

      p(case 0
      in Integer
        true
      end)

      p(case "0"
      in /0/
        true
      end)

      p(case "0"
      in -> s { s == "0" }
        true
      end)