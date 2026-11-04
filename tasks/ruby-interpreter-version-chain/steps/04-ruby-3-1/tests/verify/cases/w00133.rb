begin
  p({a: 1, b: 2}.each_cons(2) { })
rescue Exception => e
  p e.class
end
