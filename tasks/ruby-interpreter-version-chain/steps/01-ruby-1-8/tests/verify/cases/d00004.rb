begin
  p "hello".each { |x| }
rescue Exception => e
  p e.class
end
