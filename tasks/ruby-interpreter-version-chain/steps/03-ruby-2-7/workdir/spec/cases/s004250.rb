# frozen_string_literal: false
class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
dynamic = %w(this string is frozen).join(' ')
    dynamic.instance_variable_set(:@a, 1)
    dynamic.freeze

    p((-dynamic))
    p((-dynamic))
    p((-dynamic))