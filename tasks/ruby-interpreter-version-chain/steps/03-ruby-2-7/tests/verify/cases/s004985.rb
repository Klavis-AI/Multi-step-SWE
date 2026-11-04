class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
klass = Class.new do
      def ===(o)
        true
      end
      private :===
    end

    p(case 1
    when klass.new
      :called
    end)