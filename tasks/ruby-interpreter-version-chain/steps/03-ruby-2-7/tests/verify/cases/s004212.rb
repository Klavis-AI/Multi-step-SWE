# -*- encoding: utf-8 -*-
# frozen_string_literal: false
class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
str = "hedgehog"
    str.setbyte(-3, 108)
    p(str)

    # copy-on-write case
    str1, str2 = "fooXbar".split("X")
    str2.setbyte(-1, 50)
    p(str2)
    p(str1)