begin
  p "abc".equal?("abc")
rescue Exception => e
  p e.class
end
