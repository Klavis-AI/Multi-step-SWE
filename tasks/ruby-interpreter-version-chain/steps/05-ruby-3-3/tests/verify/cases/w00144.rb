begin
  p({a: 1, b: 2}.zip([9]))
rescue Exception => e
  p e.class
end
