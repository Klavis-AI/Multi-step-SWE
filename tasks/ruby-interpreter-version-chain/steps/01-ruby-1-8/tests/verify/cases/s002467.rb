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

md = /(.)(.)(\d+)(\d)/.match("THX1138.")

    p(md[0])
    p(md[1])
    p(md[2])
    p(md[-3])
    p(md[10000])
    p(md[-10000])