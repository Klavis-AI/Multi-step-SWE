class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
@obj = Object.new
    def @obj.foo0(&a)
      [a ? a.call : nil]
    end
    def @obj.foo1(a, &b)
      [a, b ? b.call : nil]
    end
    def @obj.foo2(a, b, &c)
      [a, b, c ? c.call : nil]
    end
    def @obj.foo3(a, b, c, &d)
      [a, b, c, d ? d.call : nil]
    end
    def @obj.foo4(a, b, c, d, &e)
      [a, b, c, d, e ? e.call : nil]
    end
p((obj = @obj).foo1(obj = nil))
    p((obj = @obj).foo2(obj = nil, obj = nil))
    p((obj = @obj).foo3(obj = nil, obj = nil, obj = nil))
    p((obj = @obj).foo4(obj = nil, obj = nil, obj = nil, obj = nil))