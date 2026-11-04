class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
o = nil

    suppress_warning do
      p((o =~ /Object/))
      p((o =~ 'Object'))
      p((o =~ Object))
      p((o =~ Object.new))
      p((o =~ nil))
      p((o =~ false))
      p((o =~ true))
    end