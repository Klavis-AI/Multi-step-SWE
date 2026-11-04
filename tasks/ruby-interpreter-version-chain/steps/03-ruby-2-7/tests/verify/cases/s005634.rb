class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
def m(a, b, *c, d, e) [a, b, c, d, e] end
args = [3, 4]
      p(m(1, args.shift, *args, 4, 5))