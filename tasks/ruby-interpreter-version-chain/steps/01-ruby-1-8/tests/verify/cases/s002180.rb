class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((13 % 4))
      p((4 % 13))

      p((13 % 4.0))
      p((4 % 13.0))

      p((-200 % 256))
      p((-1000 % 512))

      p((-200 % -256))
      p((-1000 % -512))

      p((200 % -256))
      p((1000 % -512))

      p((13 % -4.0))
      p((4 % -13.0))

      p((-13 % -4.0))
      p((-4 % -13.0))

      p((-13 % 4.0))
      p((-4 % 13.0))

      p((1 % 2.0))
      p((200 % bignum_value))

      p((4 % bignum_value(10)))
      p((4 % -bignum_value(10)))
      p((-4 % bignum_value(10)))
      p((-4 % -bignum_value(10)))