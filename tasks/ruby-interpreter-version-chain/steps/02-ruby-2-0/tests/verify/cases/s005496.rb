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

h = {() => ()}
    p(h.keys)
    p(h.values)
    p(h[nil])

    h = {() => :value}
    p(h.keys)
    p(h.values)
    p(h[nil])

    h = {key: ()}
    p(h.keys)
    p(h.values)
    p(h[:key])