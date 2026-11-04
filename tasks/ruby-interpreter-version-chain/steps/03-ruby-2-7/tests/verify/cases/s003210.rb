class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
@proc_add = Proc.new {|x,y,z| (x||0) + (y||0) + (z||0) }
    @lambda_add = -> x, y, z { (x||0) + (y||0) + (z||0) }
curried = @proc_add.curry.call(1, 2)

    p(instance_exec(3, &curried))