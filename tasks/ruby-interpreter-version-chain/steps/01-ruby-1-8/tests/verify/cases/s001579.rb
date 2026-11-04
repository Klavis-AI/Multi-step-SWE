class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
hash_with_members = { 'a' => false, 'b' => false, 'c' => true, 'd' => false }
      p(hash_with_members.any? {|k,v| v == true})