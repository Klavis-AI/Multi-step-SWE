# encoding: binary
class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(Regexp.quote("a.\x85b.".dup.force_encoding("US-ASCII")))
    p(Regexp.quote("a.\x80".dup.force_encoding("UTF-8")))