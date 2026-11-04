begin
  p "日本語".each { |x| }
rescue Exception => e
  p e.class
end
