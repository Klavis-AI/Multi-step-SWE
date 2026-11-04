class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(13.divmod(4))
      p(4.divmod(13))

      p(13.divmod(4.0))
      p(4.divmod(13.0))

      p(1.divmod(2.0))
      p(200.divmod(bignum_value))