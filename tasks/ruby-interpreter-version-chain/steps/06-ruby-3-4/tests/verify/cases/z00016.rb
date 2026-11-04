begin
  p [10, 20, 30].fetch_values(0, 2)
rescue Exception => e
  p e.class
end
