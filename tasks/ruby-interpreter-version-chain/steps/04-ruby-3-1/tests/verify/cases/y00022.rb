begin
  a = nil
  p a&.length
  p "ab"&.length
  p a&.length&.to_s
rescue Exception => e
  p e.class
end
