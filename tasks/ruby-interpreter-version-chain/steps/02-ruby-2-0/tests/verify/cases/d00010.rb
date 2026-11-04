begin
  p "hello".ord
rescue Exception => e
  p e.class
end
