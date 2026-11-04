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

binary_hash = HashStringsBinary.literal_hash
    utf8_hash = HashStringsUTF8.literal_hash
    usascii_hash = HashStringsUSASCII.literal_hash

    p(binary_hash.keys.first.encoding)
    p(binary_hash.keys.first)
    p(utf8_hash.keys.first.encoding)
    p(utf8_hash.keys.first)
    p(usascii_hash.keys.first.encoding)