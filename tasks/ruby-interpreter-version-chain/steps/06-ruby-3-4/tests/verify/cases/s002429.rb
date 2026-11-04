class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
match_data = /(.)(.)(\d+)(\d)/.match("THX1138.")
        p(match_data.byteend(0))
        p(match_data.byteend(2))