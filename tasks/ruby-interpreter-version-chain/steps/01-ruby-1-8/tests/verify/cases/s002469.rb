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

p(/(.)(.)(\d+)(\d)/.match("THX1138.")[1..3])
    p(/(.)(.)(\d+)(\d)/.match("THX1138.")[3..10])
    p(/(.)(.)(\d+)(\d)/.match("THX1138.")[-30..2])
    p(/(.)(.)(\d+)(\d)/.match("THX1138.")[3..1])