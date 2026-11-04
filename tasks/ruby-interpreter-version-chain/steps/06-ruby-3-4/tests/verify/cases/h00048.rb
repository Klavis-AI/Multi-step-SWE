begin
  p [[1, 2], [3, 4]].to_h
rescue Exception => e
  p e.class
end
