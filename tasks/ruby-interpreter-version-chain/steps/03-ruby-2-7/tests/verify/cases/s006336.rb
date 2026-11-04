class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
klass = Class.new do
      attr_reader :foo
      def foo=(val)
        @foo = val
        42
      end
    end
    obj = klass.new

    p((obj&.foo = 3))
    p(obj.foo)

    obj = nil
    p((obj&.foo = 3))