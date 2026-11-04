class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
module YieldSpecs
  class Yielder
    def z
      yield
    end

    def ze(&block)
      block = proc { block }
      yield
    end

    def s(a)
      yield(a)
    end

    def m(a, b, c)
      yield(a, b, c)
    end

    def r(a)
      yield(*a)
    end

    def k(a)
      yield(*a, b: true)
    end

    def rs(a, b, c)
      yield(a, b, *c)
    end

    def self.define_deep(&inned_block)
      define_method 'deep' do |v|
        # should yield to inner_block
        yield v
      end
    end

    define_deep { |v| v * 2}
  end
end

@y = YieldSpecs::Yielder.new
p(@y.r([[1, 2]]) { |*a| a })
      p(@y.r([nil]) { |*a| a })
      p(@y.r([[]]) { |*a| a })