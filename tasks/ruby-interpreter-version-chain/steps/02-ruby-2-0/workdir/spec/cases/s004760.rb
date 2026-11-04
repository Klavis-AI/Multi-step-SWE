class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
klass = Class.new { attr_accessor :b }
        @a    = klass.new
ScratchPad.record []
        @a.b = 1

        (ScratchPad << :evaluated; @a).b += 2

        p(ScratchPad.recorded)
        p(@a.b)