begin
  p([1, 2].to_s)
rescue Exception => e
  p e.class
end
