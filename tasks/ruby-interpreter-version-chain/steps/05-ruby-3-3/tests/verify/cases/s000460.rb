# encoding: binary
class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p([ [Encoding, "abc".dup.force_encoding("BINARY"), "123".dup.force_encoding("US-ASCII"), Encoding::BINARY],
        [Encoding, "123".dup.force_encoding("US-ASCII"), "abc".dup.force_encoding("BINARY"), Encoding::US_ASCII]
      ])