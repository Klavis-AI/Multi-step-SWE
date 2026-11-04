# -*- encoding: utf-8 -*-
class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
ScratchPad.record []
        /a/.match("b") {|m| ScratchPad << m }
        p(ScratchPad.recorded)