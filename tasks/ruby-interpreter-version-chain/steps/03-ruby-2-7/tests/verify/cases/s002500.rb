class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
@match_data = /(.)(.)(\d+)(\d)/.match("THX1138.")
match_data = "abc def ghi".match(/(?<first>\w+)\s+(?<last>\w+)\s+(\w+)/)

    p(match_data.inspect)