class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
ScratchPad.record []
        Enumerator.new do |y|
          ScratchPad << y.yield(1)
        end.to_a

        p(ScratchPad.recorded)