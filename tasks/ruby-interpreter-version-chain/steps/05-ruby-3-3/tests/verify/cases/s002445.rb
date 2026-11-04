class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
m = /\A\u3042(.)(.)?(.)\z/.match("\u3042\u3043\u3044")

    p(m.byteoffset(1))
    p(m.byteoffset(3))