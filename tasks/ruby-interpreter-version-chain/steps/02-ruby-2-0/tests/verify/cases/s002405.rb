# -*- encoding: utf-8 -*-
class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
match_data = /(?<a>.)(.)(?<b>\d+)(\d)/.match("THX1138.")
      p(match_data.begin("a"))
      p(match_data.begin("b"))