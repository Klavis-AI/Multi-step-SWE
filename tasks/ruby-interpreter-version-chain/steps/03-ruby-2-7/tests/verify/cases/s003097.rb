class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
ScratchPad.record []

      Module.new do
        Class.new do
          ScratchPad << "1".to_s
          using Module.new {
            refine String do
              def to_s; "hello from refinement"; end
            end
          }
          ScratchPad << "1".to_s
        end
      end

      p(ScratchPad.recorded)