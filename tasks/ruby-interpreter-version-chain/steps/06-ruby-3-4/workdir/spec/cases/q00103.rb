begin
  p "x".taint.class
rescue Exception => e
  p e.class
end
