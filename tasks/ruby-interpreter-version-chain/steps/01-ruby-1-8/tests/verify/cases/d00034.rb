begin
  p "a".each { |x| }
rescue Exception => e
  p e.class
end
