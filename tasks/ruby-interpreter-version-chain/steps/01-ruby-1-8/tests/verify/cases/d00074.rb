begin
  p "naïve".each { |x| }
rescue Exception => e
  p e.class
end
