begin
  p([1, 2, 3].each_with_index { })
rescue Exception => e
  p e.class
end
