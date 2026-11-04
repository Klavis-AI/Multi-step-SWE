class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(0b101001101[1, -1])
        p(0b101001101[2, -1])
        p(0b101001101[3, -1])

        p(0b101001101[3,   -5])
        p(0b101001101[3,  -15])
        p(0b101001101[3, -125])