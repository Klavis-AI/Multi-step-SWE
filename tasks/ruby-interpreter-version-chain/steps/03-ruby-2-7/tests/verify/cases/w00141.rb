begin
  p({a: 1, b: 2}.minmax)
rescue Exception => e
  p e.class
end
