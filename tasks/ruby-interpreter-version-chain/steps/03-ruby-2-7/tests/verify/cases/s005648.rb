class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
def foo(a, b, c, hsh)
        hsh[:key]
      end

      p(foo(1, 2, 3, key: 42))