begin
  p([[1, 2], [3, 4]].each_entry { })
rescue Exception => e
  p e.class
end
