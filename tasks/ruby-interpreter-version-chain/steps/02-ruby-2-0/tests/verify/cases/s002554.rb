class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
module MatchDataSpecs
  class MyString < String; end
end

str = MatchDataSpecs::MyString.new("THX1138.")
    /(.)(.)(\d+)(\d)/.match(str)[0..-1].to_a.each { |m| p(m) }