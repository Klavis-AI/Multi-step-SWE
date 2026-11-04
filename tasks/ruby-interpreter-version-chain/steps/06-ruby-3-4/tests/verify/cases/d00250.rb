begin
  p([1, 2].fetch_values(0))
rescue Exception => e
  p e.class
end
