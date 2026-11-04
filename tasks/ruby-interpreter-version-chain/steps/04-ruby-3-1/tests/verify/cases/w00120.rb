begin
  p((1..4).each_cons(2) { })
rescue Exception => e
  p e.class
end
