class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
%w{'!', '!=', '!~'}.each do |sym|
      p(sym.to_sym.to_s)
    end