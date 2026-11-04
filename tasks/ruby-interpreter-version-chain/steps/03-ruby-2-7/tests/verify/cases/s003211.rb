class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
@proc_add = Proc.new {|x,y,z| (x||0) + (y||0) + (z||0) }
    @lambda_add = -> x, y, z { (x||0) + (y||0) + (z||0) }
l = -> a, b, c { a+b+c }
    l1 = l.curry.call(1)
    # the l1 currying seems unnecessary, but it triggered the original issue
    l2 = l1.curry.call(2)

    p(l2.curry.call(3))
    p(l1.curry.call(2,3))