class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
acc = []
    (1..10).max {|a,b| acc << [a,b]; a }
    acc.flatten!
    (1..10).each do |value|
      p(acc.include?(value))
    end