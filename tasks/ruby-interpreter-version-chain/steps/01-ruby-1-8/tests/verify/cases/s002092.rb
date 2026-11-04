class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((1 == 1))
      p((9 == 5))

      # Actually, these call Float#==, Integer#== etc.
      p((9 == 9.0))
      p((9 == 9.01))

      p((10 == bignum_value))