class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(:a.equal?(:a))
    p(Set.new.compare_by_identity.merge([:a, :a]).to_a)

    ary1 = [1]
    ary2 = [1]
    p(ary1.equal?(ary2))
    p(Set.new.compare_by_identity.merge([ary1, ary2]).to_a.sort)