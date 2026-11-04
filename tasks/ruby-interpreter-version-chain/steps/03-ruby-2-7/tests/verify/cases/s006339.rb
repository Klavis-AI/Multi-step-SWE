class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
klass = Class.new do
      attr_accessor :m

      def initialize
        @m = true
      end
    end

    obj = klass.new

    p((obj&.m &&= false))
    p(obj.m)

    obj = nil
    p((obj&.m &&= false))
    p(obj)