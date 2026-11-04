class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
exist = [2,3]
    processed = []
    order = []
    [1,2,3,4].each do |x|
      order << x
      begin
        processed << x
        if exist.include?(x)
          raise StandardError, "included"
        end
      rescue StandardError
        exist.delete(x)
        redo
      end
    end
    p(processed)
    p(exist)
    p(order)