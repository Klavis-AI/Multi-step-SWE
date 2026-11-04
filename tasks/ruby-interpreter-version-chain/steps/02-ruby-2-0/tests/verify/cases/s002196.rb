class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(20.ord)
    p(40.ord)

    p(0.ord)
    p((-10).ord)

    p(?a.ord)
    p(?Z.ord)

    p(bignum_value.ord)
    p((-bignum_value).ord)