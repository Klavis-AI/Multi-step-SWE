class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
klass = Class.new do
      attr_reader :m

      def initialize
        @m = 0
      end

      def m=(v)
        @m = v
        42
      end
    end

    obj = klass.new

    obj&.m += 3
    p(obj.m)

    obj = nil
    p((obj&.m += 3))