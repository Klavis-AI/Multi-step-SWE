begin
  p({a: 1, b: 2}.except(:a))
rescue Exception => e
  p e.class
end
