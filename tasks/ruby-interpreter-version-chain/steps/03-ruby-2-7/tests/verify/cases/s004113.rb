# -*- encoding: utf-8 -*-
class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
@utf8 = "東京"
    @ascii = 'Tokyo'
    @utf8_ascii = @utf8 + @ascii
bytes = []
    p(@utf8.bytes {|b| bytes << b})
    p(bytes)