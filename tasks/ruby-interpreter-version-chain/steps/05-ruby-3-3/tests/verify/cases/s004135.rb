# -*- encoding: utf-8 -*-
# frozen_string_literal: false
class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p("hello".bytesplice(1..2, "HELLO", -5...-5))
    p("hello".bytesplice(1..2, "HELLO", 0...0))
    p("hello".bytesplice(1..2, "HELLO", 0..0))
    p("hello".bytesplice(1..2, "HELLO", 0...1))
    p("hello".bytesplice(1..2, "HELLO", 0..1))
    p("hello".bytesplice(1..2, "HELLO", 0..-1))
    p("hello".bytesplice(1..2, "HELLO", 0...5))
    p("hello".bytesplice(1..2, "HELLO", 0...6))