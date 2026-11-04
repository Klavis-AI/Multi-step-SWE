class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
fiber = Fiber.new do
      begin
        raise "an error in a Fiber"
      rescue
        Fiber.yield :first
      end

      :second
    end

    p(fiber.resume)
    p(fiber.resume)