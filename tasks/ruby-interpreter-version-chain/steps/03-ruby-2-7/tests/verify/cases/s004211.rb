# -*- encoding: utf-8 -*-
# frozen_string_literal: false
class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
str = "glark"
    p(str.valid_encoding?)
    str.setbyte(2,253)
    p(str.valid_encoding?)

    str = "ABC"
    str.setbyte(0, 0x20) # ' '
    p(str)
    str.setbyte(0, 0xE3)
    p(str)