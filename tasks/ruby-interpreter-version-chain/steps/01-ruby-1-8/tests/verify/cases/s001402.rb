class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
klass = Class.new do
      attr_reader :call_count
      def coerce(other)
        @call_count ||= 0
        @call_count += 1
        [other, 42.0]
      end
    end

    coercible = klass.new
    p((2.33 <=> coercible))
    p((42.0 <=> coercible))
    p((43.0 <=> coercible))
    p(coercible.call_count)