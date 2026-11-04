begin
  p "héllo".each { |x| }
rescue Exception => e
  p e.class
end
