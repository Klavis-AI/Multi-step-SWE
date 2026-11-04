begin
  p({a: 1, b: 2, c: 3}.except(:b, :c))
rescue Exception => e
  p e.class
end
