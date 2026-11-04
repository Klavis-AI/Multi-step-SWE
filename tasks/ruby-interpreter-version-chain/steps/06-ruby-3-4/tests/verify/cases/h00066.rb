begin
  p [1, 2, 3].fetch_values(0, 2)
rescue Exception => e
  p e.class
end
