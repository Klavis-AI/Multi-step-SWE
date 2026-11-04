begin
  s = "ab".dup
  s.append_as_bytes("cd")
  p s
rescue Exception => e
  p e.class
end
