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
a = 0
    p((a += 1; @obj).foo1(a))
    p((a += 1; @obj).foo2(a, a))
    p((a += 1; @obj).foo3(a, a, a))
    p((a += 1; @obj).foo4(a, a, a, a))
    p(a)