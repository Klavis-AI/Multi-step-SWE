# frozen_string_literal: false
class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
origin = "this is a string"
    dynamic = %w(this is a string).join(' ')

    p(origin)
    p((-origin))