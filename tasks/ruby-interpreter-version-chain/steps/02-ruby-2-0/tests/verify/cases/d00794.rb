begin
  s = "héllo".dup
  s.force_encoding("US-ASCII")
  p s.encoding.name
  p s.valid_encoding?
  p s.length
  p s.bytesize
  p s.ascii_only?
rescue Exception => e
  p e.class
end
