class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
# encoding: binary

module HashStringsBinary
  def self.literal_hash
    {"foo" => "bar"}
  end
end

# encoding: utf-8

module HashStringsUTF8
  def self.literal_hash
    {"foo" => "bar"}
  end
end

# encoding: us-ascii

module HashStringsUSASCII
  def self.literal_hash
    {"foo" => "bar"}
  end
end

h = {a: 1, b: 2,}
    p(h.size)
    p(h)