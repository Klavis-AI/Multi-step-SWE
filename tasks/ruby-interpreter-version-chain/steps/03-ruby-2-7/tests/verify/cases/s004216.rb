# -*- encoding: utf-8 -*-
# frozen_string_literal: false
class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
str1 = "hedgehog"
    str2 = String.new(str1)
    str2.setbyte(0, 108)
    p(str2)
    p(str2)
    p(str1)
    p(str1)