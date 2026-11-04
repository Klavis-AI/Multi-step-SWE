class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
def m(a = nil, b = {}, v: false)
      [a, b, v]
    end

    h = { "key" => "value" }
    p(m(:a, h))
    p(m(:a, h, v: true))
    p(m(v: true))