class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
counter = 0

    begin
      counter += 1
      raise "An exception"
    rescue
      retry unless counter == 7
    end

    p(counter)