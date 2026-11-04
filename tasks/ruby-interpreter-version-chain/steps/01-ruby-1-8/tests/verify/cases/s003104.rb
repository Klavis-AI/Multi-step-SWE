class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
result = nil

      Module.new do
        if false
          using Module.new {
            refine String do
              def to_s; "hello from refinement"; end
            end
          }
        end
        result = "1".to_s
      end

      p(result)