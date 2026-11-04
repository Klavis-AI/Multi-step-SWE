class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
refinement = Module.new do
        refine String do
          def to_s
            "hello from refinement"
          end
        end
      end

      result = []
      klass = Class.new do
        using refinement
        result << "1".to_s
      end

      klass.class_eval do
        result << "1".to_s
      end

      p(result)