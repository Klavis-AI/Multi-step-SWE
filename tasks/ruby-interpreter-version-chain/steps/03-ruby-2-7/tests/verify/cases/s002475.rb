class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
module MatchDataSpecs
  class MyString < String; end
end

md = 'haystack'.match(/(?<t>t(?<a>ack))/)
    p(md['a'])
    p(md['t'])