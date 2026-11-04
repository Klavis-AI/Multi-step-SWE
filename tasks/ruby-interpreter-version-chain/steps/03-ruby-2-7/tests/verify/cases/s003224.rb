class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
@proc_add = proc { |x,y,z| (x||0) + (y||0) + (z||0) }
    @lambda_add = -> x, y, z { (x||0) + (y||0) + (z||0) }
p(@proc_add.curry(1).arity)
    p(@proc_add.curry(2).arity)
    p(@lambda_add.curry(3).arity)
    l = -> *a { }
    p(l.curry(3).arity)