class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
@a = [2, 4, 6, 8, 10]

    @e_strs = ["333", "22", "666666", "1", "55555", "1010101010"]
    @e_ints = [ 333,   22,   666666,        55555,   1010101010]
p([55].min)

    p([11,99].min)
    p([99,11].min)
    p([2, 33, 4, 11].min)

    p([1,2,3,4,5].min)
    p([5,4,3,2,1].min)
    p([4,1,3,5,2].min)
    p([5,5,5,5,5].min)

    p(["aa","tt"].min)
    p(["tt","aa"].min)
    p(["2","33","4","11"].min)

    p(@e_strs.min)
    p(@e_ints.min)