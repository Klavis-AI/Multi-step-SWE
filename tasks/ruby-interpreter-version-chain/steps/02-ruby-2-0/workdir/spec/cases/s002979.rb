class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
klass = Module.new do
      define_singleton_method :a do
        42
      end
      define_singleton_method(:b, -> x { 2*x })
    end

    p(klass.a)
    p(klass.b(10))