begin
  s = "ab".dup
  s.append_as_bytes("c")
  p s
rescue Exception => e
  p e.class
end
