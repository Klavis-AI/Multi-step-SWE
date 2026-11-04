class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
klass = Class.new do
      def m
        yield
      end

      def to_proc
        :m.to_proc.call(self) { :value }
      end
    end
    p(klass.new.to_proc)