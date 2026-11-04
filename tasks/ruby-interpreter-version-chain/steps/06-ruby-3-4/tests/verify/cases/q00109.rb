begin
  p [10, 20].fetch_values(0, 5)
rescue Exception => e
  p e.class
end
