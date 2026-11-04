begin
  p([1, 2, 3].each_cons(2) { })
rescue Exception => e
  p e.class
end
