begin
  p "ab	c".each { |x| }
rescue Exception => e
  p e.class
end
