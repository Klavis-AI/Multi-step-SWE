class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
retry_first = true
    retry_second = true
    results = []
    begin
      results << 1
      raise
    rescue
      results << 2
      if retry_first
        results << 3
        retry_first = false
        retry
      end
      begin
        results << 4
        raise
      rescue
        results << 5
        if retry_second
          results << 6
          retry_second = false
          retry
        end
      end
    end

    p(results)