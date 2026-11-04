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

p(/(.)(.)(\d+)(\d)/.match("THX1138.")[1, 2])
    p(/(.)(.)(\d+)(\d)/.match("THX1138.")[-3, 2])

    # negative index is larger than the number of match values
    p(/(.)(.)(\d+)(\d)/.match("THX1138.")[-30, 2])

    # positive index larger than number of match values
    p(/(.)(.)(\d+)(\d)/.match("THX1138.")[5, 2])
    p(/(.)(.)(\d+)(\d)/.match("THX1138.")[6, 2])
    p(/(.)(.)(\d+)(\d)/.match("THX1138.")[30, 2])

    # length argument larger than number of match values is capped to match value length
    p(/(.)(.)(\d+)(\d)/.match("THX1138.")[3, 10])

    p(/(.)(.)(\d+)(\d)/.match("THX1138.")[3, 0])

    p(/(.)(.)(\d+)(\d)/.match("THX1138.")[3, -1])
    p(/(.)(.)(\d+)(\d)/.match("THX1138.")[3, -30])