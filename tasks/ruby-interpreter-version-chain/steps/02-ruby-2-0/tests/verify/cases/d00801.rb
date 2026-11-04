begin
  s = "日本".dup
  s.force_encoding("BINARY")
  p s.encoding.name
  p s.valid_encoding?
  p s.length
  p s.bytesize
  p s.ascii_only?
rescue Exception => e
  p e.class
end
