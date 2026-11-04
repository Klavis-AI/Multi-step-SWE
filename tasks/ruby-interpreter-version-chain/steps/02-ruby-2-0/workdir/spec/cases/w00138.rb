begin
  p({a: 1, b: 2}.each_with_index { })
rescue Exception => e
  p e.class
end
