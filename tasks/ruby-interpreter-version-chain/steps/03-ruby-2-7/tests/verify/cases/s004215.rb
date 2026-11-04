# -*- encoding: utf-8 -*-
# frozen_string_literal: false
class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
chr = "\u{998}"
    p(chr.bytesize)
    chr.setbyte(2, 150)
    p(chr)