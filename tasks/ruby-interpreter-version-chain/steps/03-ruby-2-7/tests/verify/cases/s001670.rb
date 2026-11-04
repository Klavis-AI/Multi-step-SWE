class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
h = {}
    h[:foo] = [ { bar: [ 1 ] }, [ obj = Object.new, 'str' ] ]
    def obj.dig(*args); [ 42 ] end

    p(h.dig(:foo, 0, :bar))
    p(h.dig(:foo, 0, :bar, 0))
    p(h.dig(:foo, 1, 1))
    # MRI does not recurse values returned from `obj.dig`
    p(h.dig(:foo, 1, 0, 0))
    p(h.dig(:foo, 1, 0, 0, 10))