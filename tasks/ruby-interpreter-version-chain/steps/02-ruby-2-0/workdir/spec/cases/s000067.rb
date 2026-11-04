# encoding: binary
class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
n = [ 65, 66, 67 ] # result without buffer is "ABC"

    buffer = +""
    p(n.pack("ccc", buffer: buffer))

    buffer = +"123"
    p(n.pack("ccc", buffer: buffer))

    buffer = +"12345"
    p(n.pack("ccc", buffer: buffer))