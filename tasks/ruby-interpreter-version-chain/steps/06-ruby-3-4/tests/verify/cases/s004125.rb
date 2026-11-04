# -*- encoding: utf-8 -*-
# frozen_string_literal: false
class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p("hello".bytesplice(-5...-5, "xxx"))
    p("hello".bytesplice(0...0, "xxx"))
    p("hello".bytesplice(0..0, "xxx"))
    p("hello".bytesplice(0...1, "xxx"))
    p("hello".bytesplice(0..1, "xxx"))
    p("hello".bytesplice(0..-1, "xxx"))
    p("hello".bytesplice(0...5, "xxx"))
    p("hello".bytesplice(0...6, "xxx"))